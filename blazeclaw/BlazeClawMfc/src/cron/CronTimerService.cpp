#include "pch.h"

#include "CronTimerService.h"

#include <chrono>
#include <cmath>

namespace {
	constexpr std::int64_t kMinuteMs = 60'000;
	constexpr std::int64_t kHourMs = 60 * kMinuteMs;
	constexpr std::int64_t kDayMs = 24 * kHourMs;
}

namespace blazeclaw::cron {

	namespace {
		CronJson& EnsureStateObject(CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				job["state"] = CronJson::object();
			}
			return job["state"];
		}

		std::optional<std::int64_t> ReadNextRunAtMs(const CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				return std::nullopt;
			}
			return TryReadInt64Field(job["state"], "nextRunAtMs");
		}

		std::optional<std::pair<int, int>> ParseCronMinuteHour(
			const std::string& exprRaw) {
			const std::string expr = TrimCopy(exprRaw);
			if (expr.empty()) {
				return std::nullopt;
			}

			std::istringstream stream(expr);
			std::vector<std::string> parts;
			std::string token;
			while (stream >> token) {
				parts.push_back(token);
			}
			if (parts.size() < 2) {
				return std::nullopt;
			}

			if (parts[0] == "*" && parts[1] == "*") {
				return std::make_pair(-1, -1);
			}

			try {
				const int minute = std::stoi(parts[0]);
				const int hour = std::stoi(parts[1]);
				if (minute < 0 || minute > 59 || hour < 0 || hour > 23) {
					return std::nullopt;
				}
				return std::make_pair(minute, hour);
			}
			catch (...) {
				return std::nullopt;
			}
		}

		std::int64_t ResolveEveryAnchorMs(const CronJson& schedule, const CronJson& job) {
			const auto explicitAnchor = TryReadInt64Field(schedule, "anchorMs");
			if (explicitAnchor.has_value() && explicitAnchor.value() >= 0) {
				return explicitAnchor.value();
			}

			const auto createdAtMs = TryReadInt64Field(job, "createdAtMs");
			if (createdAtMs.has_value() && createdAtMs.value() >= 0) {
				return createdAtMs.value();
			}

			return 0;
		}

		std::int64_t ComputeNextEvery(
			const CronJson& schedule,
			const CronJson& job,
			const std::int64_t nowMs) {
			const std::int64_t everyMs =
				(std::max)(static_cast<std::int64_t>(1000),
					TryReadInt64Field(schedule, "everyMs").value_or(60'000));

			const auto lastRunAtMs =
				job.contains("state") && job["state"].is_object()
				? TryReadInt64Field(job["state"], "lastRunAtMs")
				: std::nullopt;
			if (lastRunAtMs.has_value() && lastRunAtMs.value() + everyMs > nowMs) {
				return lastRunAtMs.value() + everyMs;
			}

			const std::int64_t anchorMs = ResolveEveryAnchorMs(schedule, job);
			if (anchorMs >= nowMs) {
				return anchorMs;
			}

			const std::int64_t elapsed = nowMs - anchorMs;
			const std::int64_t ticks = elapsed / everyMs;
			return anchorMs + (ticks + 1) * everyMs;
		}

		std::optional<std::int64_t> ComputeNextAt(
			const CronJson& schedule,
			const CronJson& job,
			const std::int64_t nowMs) {
			const auto atMs =
				TryReadInt64Field(schedule, "atMs").has_value()
				? TryReadInt64Field(schedule, "atMs")
				: TryReadInt64Field(schedule, "at");
			if (!atMs.has_value()) {
				return nowMs + 5 * 60 * 1000;
			}

			const auto lastStatus =
				job.contains("state") && job["state"].is_object()
				? ToLowerCopy(job["state"].value("lastStatus", std::string()))
				: std::string();
			const auto lastRunAtMs =
				job.contains("state") && job["state"].is_object()
				? TryReadInt64Field(job["state"], "lastRunAtMs")
				: std::nullopt;

			if (lastStatus == "ok" && lastRunAtMs.has_value() && atMs.value() <= lastRunAtMs.value()) {
				return std::nullopt;
			}

			return atMs.value();
		}

		std::optional<std::int64_t> ComputeNextCron(
			const CronJson& schedule,
			const std::int64_t nowMs) {
			const std::string expr =
				TrimCopy(schedule.value("expr", std::string("* * * * *")));
			const auto parsed = ParseCronMinuteHour(expr);
			if (!parsed.has_value()) {
				return nowMs + kMinuteMs;
			}

			const int minute = parsed->first;
			const int hour = parsed->second;
			if (minute < 0 || hour < 0) {
				return ((nowMs / kMinuteMs) + 1) * kMinuteMs;
			}

			const std::int64_t dayStart = (nowMs / kDayMs) * kDayMs;
			std::int64_t candidate = dayStart + hour * kHourMs + minute * kMinuteMs;
			if (candidate <= nowMs) {
				candidate += kDayMs;
			}

			const auto staggerMs = TryReadInt64Field(schedule, "staggerMs");
			if (staggerMs.has_value() && staggerMs.value() > 0) {
				candidate += (std::abs(static_cast<int>(candidate % staggerMs.value())));
			}

			return candidate;
		}
	}

	std::optional<std::int64_t> CronTimerService::ComputeNextRunAtMs(
		const CronJson& job,
		const std::int64_t nowMs) const {
		if (!job.value("enabled", true)) {
			return std::nullopt;
		}
		if (!job.contains("schedule") || !job["schedule"].is_object()) {
			return std::nullopt;
		}

		const CronJson& schedule = job["schedule"];
		const std::string kind =
			ToLowerCopy(TrimCopy(schedule.value("kind", std::string())));

		if (kind == "every") {
			return ComputeNextEvery(schedule, job, nowMs);
		}

		if (kind == "at") {
			return ComputeNextAt(schedule, job, nowMs);
		}

		if (kind == "cron") {
			return ComputeNextCron(schedule, nowMs);
		}

		return std::nullopt;
	}

	bool CronTimerService::RecomputeSchedules(
		CronJson& jobs,
		const std::int64_t nowMs) const {
		bool changed = false;
		for (auto& job : jobs) {
			CronJson& state = EnsureStateObject(job);
			const std::optional<std::int64_t> nextRunAtMs =
				ComputeNextRunAtMs(job, nowMs);
			const CronJson nextJson =
				nextRunAtMs.has_value() ? CronJson(nextRunAtMs.value()) : CronJson(nullptr);
			if (!state.contains("nextRunAtMs") || state["nextRunAtMs"] != nextJson) {
				state["nextRunAtMs"] = nextJson;
				changed = true;
			}
		}

		return changed;
	}

	std::int64_t CronTimerService::ComputeNextWakeAtMs(const CronJson& jobs) const {
		std::int64_t nextWakeAtMs = 0;
		for (const auto& job : jobs) {
			if (!job.value("enabled", true)) {
				continue;
			}
			if (!job.contains("state") || !job["state"].is_object()) {
				continue;
			}
			const auto maybeNext = TryReadInt64Field(job["state"], "nextRunAtMs");
			if (!maybeNext.has_value() || maybeNext.value() <= 0) {
				continue;
			}
			if (nextWakeAtMs <= 0 || maybeNext.value() < nextWakeAtMs) {
				nextWakeAtMs = maybeNext.value();
			}
		}
		return nextWakeAtMs;
	}

	std::size_t CronTimerService::PumpDueRuns(
		CronJson& jobs,
		CronJson& runs,
		const std::int64_t nowMs,
		const bool forceRunDue) const {
		std::size_t executed = 0;
		for (auto it = jobs.begin(); it != jobs.end();) {
			if (!(*it).is_object() || !(*it).value("enabled", true)) {
				++it;
				continue;
			}

			const std::string id = (*it).value("id", std::string());
			if (id.empty()) {
				++it;
				continue;
			}

			const std::optional<std::int64_t> nextRunAtMs = ReadNextRunAtMs(*it);
			if (!nextRunAtMs.has_value()) {
				++it;
				continue;
			}

			if (!forceRunDue && nextRunAtMs.value() > nowMs) {
				++it;
				continue;
			}

			CronJson& state = EnsureStateObject(*it);
			state["runningAtMs"] = nowMs;
			state["lastRunAtMs"] = nowMs;
			state["lastStatus"] = "ok";
			state["lastRunStatus"] = "ok";
			state["lastDurationMs"] = 0;
			state["runningAtMs"] = nullptr;

			const bool deleteAfterRun = (*it).value("deleteAfterRun", false);
			const std::string jobName = (*it).value("name", std::string());
			std::optional<std::int64_t> nextAfterRun;
			if (!deleteAfterRun) {
				nextAfterRun = ComputeNextRunAtMs(*it, nowMs);
				state["nextRunAtMs"] =
					nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr);
				(*it)["updatedAtMs"] = nowMs;
			}

			runs.push_back({
				{ "ts", nowMs },
				{ "jobId", id },
				{ "action", "finished" },
				{ "status", "ok" },
				{ "deliveryStatus", "not-requested" },
				{ "durationMs", 0 },
				{ "runAtMs", nowMs },
				{ "nextRunAtMs", deleteAfterRun ? CronJson(nullptr) : (nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr)) },
				{ "jobName", jobName },
				{ "runId", BuildCronRunId(nowMs) }
			});
			++executed;

			if (deleteAfterRun) {
				it = jobs.erase(it);
				continue;
			}

			++it;
		}

		return executed;
	}

} // namespace blazeclaw::cron
