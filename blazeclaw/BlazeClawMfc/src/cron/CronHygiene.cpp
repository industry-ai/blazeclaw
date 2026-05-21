#include "pch.h"

#include "CronHygiene.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace blazeclaw::cron {

	namespace {
		inline constexpr std::int64_t kOneMinuteMs = 60 * 1000;
		inline constexpr std::int64_t kTenYearsMs =
			static_cast<std::int64_t>(10 * 365.25 * 24 * 60 * 60 * 1000);
		inline constexpr std::int64_t kDefaultSessionRetentionMs = 24 * 60 * 60 * 1000;
		inline constexpr std::int64_t kMinSessionSweepIntervalMs = 5 * 60 * 1000;

		std::mutex g_activeJobsMutex;
		std::unordered_set<std::string> g_activeJobIds;

		std::int64_t g_lastSessionSweepAtMs = 0;

		std::optional<std::int64_t> ParseInt64Loose(const CronJson& value) {
			if (value.is_number_integer()) {
				return value.get<std::int64_t>();
			}
			if (value.is_number_unsigned()) {
				return static_cast<std::int64_t>(value.get<std::uint64_t>());
			}
			if (value.is_number_float()) {
				return static_cast<std::int64_t>(value.get<double>());
			}
			if (value.is_string()) {
				try {
					const std::string raw = TrimCopy(value.get<std::string>());
					if (raw.empty()) {
						return std::nullopt;
					}
					return std::stoll(raw);
				}
				catch (...) {
					return std::nullopt;
				}
			}
			return std::nullopt;
		}

		std::optional<std::int64_t> ParseIso8601ToUtcMs(const std::string& value) {
			const std::string trimmed = TrimCopy(value);
			if (trimmed.empty()) {
				return std::nullopt;
			}

			std::string normalized = trimmed;
			if (normalized.back() == 'Z' || normalized.back() == 'z') {
				normalized.pop_back();
			}

			std::tm tm{};
			std::istringstream stream(normalized);
			stream >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
			if (stream.fail()) {
				return std::nullopt;
			}

			const auto timePoint = std::chrono::system_clock::from_time_t(_mkgmtime(&tm));
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				timePoint.time_since_epoch()).count();
		}

		std::optional<std::int64_t> ResolveScheduleAtMs(const CronJson& schedule) {
			if (!schedule.is_object()) {
				return std::nullopt;
			}

			if (schedule.contains("atMs")) {
				return ParseInt64Loose(schedule["atMs"]);
			}

			if (schedule.contains("at") && schedule["at"].is_string()) {
				return ParseIso8601ToUtcMs(schedule["at"].get<std::string>());
			}

			return std::nullopt;
		}

		std::int64_t ResolveSessionRetentionMs() {
			char* value = nullptr;
			size_t valueLength = 0;
			if (_dupenv_s(
				&value,
				&valueLength,
				"BLAZECLAW_CRON_SESSION_RETENTION_MS") != 0 || value == nullptr) {
				return kDefaultSessionRetentionMs;
			}

			const std::string trimmed = TrimCopy(value);
			free(value);
			if (trimmed.empty()) {
				return kDefaultSessionRetentionMs;
			}

			if (trimmed == "0" || ToLowerCopy(trimmed) == "false" || ToLowerCopy(trimmed) == "off") {
				return 0;
			}

			try {
				const long long parsed = std::stoll(trimmed);
				if (parsed <= 0) {
					return 0;
				}
				return static_cast<std::int64_t>(parsed);
			}
			catch (...) {
				return kDefaultSessionRetentionMs;
			}
		}

		CronJson& EnsureStateObject(CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				job["state"] = CronJson::object();
			}
			return job["state"];
		}

		CronJson& EnsureRunSessionsArray(CronJson& state) {
			if (!state.contains("cronRunSessions") || !state["cronRunSessions"].is_array()) {
				state["cronRunSessions"] = CronJson::array();
			}
			return state["cronRunSessions"];
		}

		bool IsCronRunSessionKey(const std::string& sessionKey) {
			return sessionKey.find(":cron:") != std::string::npos &&
				sessionKey.find(":run:") != std::string::npos;
		}
	} // namespace

	CronScheduleValidation ValidateScheduleTimestamp(
		const CronJson& schedule,
		std::int64_t nowMs) {
		if (!schedule.is_object()) {
			return { true, {} };
		}

		const std::string kind = ToLowerCopy(TrimCopy(schedule.value("kind", std::string())));
		if (kind != "at") {
			return { true, {} };
		}

		// Normalized/internal schedules carry atMs only; reject only explicit ISO `at` input.
		if (!schedule.contains("at") || !schedule["at"].is_string()) {
			return { true, {} };
		}

		if (nowMs <= 0) {
			nowMs = UtcNowMs();
		}

		const auto atMs = ResolveScheduleAtMs(schedule);
		if (!atMs.has_value()) {
			return {
				false,
				"Invalid schedule.at: expected ISO-8601 timestamp or numeric atMs"
			};
		}

		const std::int64_t diffMs = atMs.value() - nowMs;
		if (diffMs < -kOneMinuteMs) {
			const std::int64_t minutesAgo = (-diffMs) / kOneMinuteMs;
			return {
				false,
				"schedule.at is in the past (" + std::to_string(minutesAgo) + " minutes ago)"
			};
		}

		if (diffMs > kTenYearsMs) {
			return {
				false,
				"schedule.at is too far in the future (maximum allowed: 10 years)"
			};
		}

		return { true, {} };
	}

	void MarkCronJobActive(const std::string& jobId) {
		const std::string trimmed = TrimCopy(jobId);
		if (trimmed.empty()) {
			return;
		}

		std::lock_guard<std::mutex> lock(g_activeJobsMutex);
		g_activeJobIds.insert(trimmed);
	}

	void ClearCronJobActive(const std::string& jobId) {
		const std::string trimmed = TrimCopy(jobId);
		if (trimmed.empty()) {
			return;
		}

		std::lock_guard<std::mutex> lock(g_activeJobsMutex);
		g_activeJobIds.erase(trimmed);
	}

	bool IsCronJobActive(const std::string& jobId) {
		const std::string trimmed = TrimCopy(jobId);
		if (trimmed.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(g_activeJobsMutex);
		return g_activeJobIds.find(trimmed) != g_activeJobIds.end();
	}

	void ResetCronActiveJobsForTests() {
		std::lock_guard<std::mutex> lock(g_activeJobsMutex);
		g_activeJobIds.clear();
		g_lastSessionSweepAtMs = 0;
	}

	void RecordCronRunSession(
		CronJson& job,
		const std::string& sessionKey,
		const std::int64_t nowMs) {
		const std::string trimmed = TrimCopy(sessionKey);
		if (trimmed.empty()) {
			return;
		}

		CronJson& state = EnsureStateObject(job);
		CronJson& sessions = EnsureRunSessionsArray(state);

		bool updatedExisting = false;
		for (auto& entry : sessions) {
			if (!entry.is_object()) {
				continue;
			}
			if (entry.value("sessionKey", std::string()) != trimmed) {
				continue;
			}
			entry["updatedAtMs"] = nowMs;
			updatedExisting = true;
			break;
		}

		if (!updatedExisting) {
			sessions.push_back({
				{ "sessionKey", trimmed },
				{ "updatedAtMs", nowMs }
			});
		}
	}

	std::size_t SweepCronRunSessions(CronJson& jobs, const std::int64_t nowMs) {
		const std::int64_t retentionMs = ResolveSessionRetentionMs();
		if (retentionMs <= 0) {
			g_lastSessionSweepAtMs = nowMs;
			return 0;
		}

		if (g_lastSessionSweepAtMs > 0 &&
			(nowMs - g_lastSessionSweepAtMs) < kMinSessionSweepIntervalMs) {
			return 0;
		}

		g_lastSessionSweepAtMs = nowMs;
		const std::int64_t cutoffMs = nowMs - retentionMs;
		std::size_t pruned = 0;

		for (auto& job : jobs) {
			if (!job.is_object()) {
				continue;
			}

			CronJson& state = EnsureStateObject(job);
			if (!state.contains("cronRunSessions") || !state["cronRunSessions"].is_array()) {
				continue;
			}

			CronJson kept = CronJson::array();
			for (const auto& entry : state["cronRunSessions"]) {
				if (!entry.is_object()) {
					continue;
				}

				const std::string sessionKey =
					TrimCopy(entry.value("sessionKey", std::string()));
				const auto updatedAtMs = TryReadInt64Field(entry, "updatedAtMs");
				if (sessionKey.empty() ||
					!updatedAtMs.has_value() ||
					updatedAtMs.value() >= cutoffMs ||
					!IsCronRunSessionKey(sessionKey)) {
					kept.push_back(entry);
					continue;
				}

				++pruned;
			}

			if (kept.empty()) {
				state.erase("cronRunSessions");
			}
			else {
				state["cronRunSessions"] = kept;
			}
		}

		return pruned;
	}

} // namespace blazeclaw::cron
