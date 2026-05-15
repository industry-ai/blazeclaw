#include "pch.h"

#include "CronTimerService.h"

#include <chrono>
#include <cmath>
#include <sstream>
#include <vector>

namespace {
	constexpr std::int64_t kMinuteMs = 60'000;
	constexpr std::int64_t kHourMs = 60 * kMinuteMs;
	constexpr std::int64_t kDayMs = 24 * kHourMs;
}

namespace blazeclaw::cron {

	namespace {
		inline constexpr std::int64_t kDefaultRetryDelayMs = 60'000;
		inline constexpr std::int64_t kDefaultFailureAlertAfter = 2;
		inline constexpr std::int64_t kDefaultFailureAlertCooldownMs = 60 * 60'000;

		bool StartsWithHttpScheme(const std::string& value) {
			const std::string lowered = ToLowerCopy(TrimCopy(value));
			return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
		}

		std::int64_t ResolveRetryDelayMs(const CronJson& retry, const std::int64_t attemptIndex) {
			auto parseDelay = [](const CronJson& value) -> std::optional<std::int64_t> {
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
						return std::stoll(TrimCopy(value.get<std::string>()));
					}
					catch (...) {
						return std::nullopt;
					}
				}
				return std::nullopt;
			};

			if (retry.contains("backoffMs") && retry["backoffMs"].is_array()) {
				const auto& backoff = retry["backoffMs"];
				if (!backoff.empty()) {
					const std::size_t index = static_cast<std::size_t>((std::max)(
						static_cast<std::int64_t>(0),
						attemptIndex - 1));
					const std::size_t bounded = (std::min)(index, backoff.size() - 1);
					const auto delay = parseDelay(backoff[bounded]);
					if (delay.has_value() && delay.value() >= 0) {
						return delay.value();
					}
				}
			}

			return kDefaultRetryDelayMs;
		}

		std::int64_t ResolveFailureAlertAfter(const CronJson& job) {
			if (!job.contains("failureAlert") || !job["failureAlert"].is_object()) {
				return kDefaultFailureAlertAfter;
			}
			return (std::max)(
				static_cast<std::int64_t>(1),
				TryReadInt64Field(job["failureAlert"], "after")
				.value_or(kDefaultFailureAlertAfter));
		}

		std::int64_t ResolveFailureAlertCooldownMs(const CronJson& job) {
			if (!job.contains("failureAlert") || !job["failureAlert"].is_object()) {
				return kDefaultFailureAlertCooldownMs;
			}
			return (std::max)(
				static_cast<std::int64_t>(0),
				TryReadInt64Field(job["failureAlert"], "cooldownMs")
				.value_or(kDefaultFailureAlertCooldownMs));
		}

		struct RunOutcome {
			std::string status = "ok";
			std::string summary;
			std::string error;
			std::string deliveryStatus = "not-requested";
			bool delivered = false;
			std::string errorCategory;
		};

		bool IsTransientErrorCategory(const std::string& errorText) {
			const std::string lowered = ToLowerCopy(errorText);
			return lowered.find("timeout") != std::string::npos ||
				lowered.find("network") != std::string::npos ||
				lowered.find("429") != std::string::npos ||
				lowered.find("rate") != std::string::npos;
		}

		CronJson& EnsureStateObject(CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				job["state"] = CronJson::object();
			}
			return job["state"];
		}

		std::optional<std::int64_t> ReadRetryPendingUntilMs(const CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				return std::nullopt;
			}
			return TryReadInt64Field(job["state"], "retryPendingUntilMs");
		}

		std::optional<std::int64_t> ReadNextRunAtMs(const CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				return std::nullopt;
			}
			return TryReadInt64Field(job["state"], "nextRunAtMs");
		}

		RunOutcome EvaluateRunOutcome(const CronJson& job) {
			RunOutcome outcome;

			if (!job.contains("payload") || !job["payload"].is_object()) {
				outcome.status = "error";
				outcome.error = "invalid payload";
				outcome.errorCategory = "invalid_payload";
				outcome.summary = "Payload missing or invalid";
				return outcome;
			}

			const CronJson& payload = job["payload"];
			const std::string payloadKind =
				ToLowerCopy(TrimCopy(payload.value("kind", std::string())));
			if (payloadKind == "systemevent") {
				const std::string text =
					TrimCopy(payload.value("text", std::string()));
				if (text.empty()) {
					outcome.status = "skipped";
					outcome.summary = "Skipped: empty systemEvent text";
					return outcome;
				}
			}
			if (payloadKind == "agentturn") {
				const std::string message =
					TrimCopy(payload.value("message", std::string()));
				if (message.empty()) {
					outcome.status = "skipped";
					outcome.summary = "Skipped: empty agentTurn message";
					return outcome;
				}
			}

			if (job.contains("delivery") && job["delivery"].is_object()) {
				const CronJson& delivery = job["delivery"];
				const std::string mode = ToLowerCopy(
					TrimCopy(delivery.value("mode", std::string("announce"))));
				if (mode == "none") {
					outcome.deliveryStatus = "not-requested";
					outcome.delivered = false;
				}
				else if (mode == "announce") {
					outcome.deliveryStatus = "delivered";
					outcome.delivered = true;
				}
				else if (mode == "webhook") {
					const std::string to =
						TrimCopy(delivery.value("to", std::string()));
					if (StartsWithHttpScheme(to)) {
						outcome.deliveryStatus = "delivered";
						outcome.delivered = true;
					}
					else {
						outcome.status = "error";
						outcome.deliveryStatus = "not-delivered";
						outcome.error = "invalid webhook delivery target";
						outcome.errorCategory = "delivery_target_invalid";
						outcome.summary = "Webhook delivery target is invalid";
					}
				}
			}

			if (outcome.status == "error" && outcome.errorCategory.empty()) {
				outcome.errorCategory = IsTransientErrorCategory(outcome.error)
					? "transient"
					: "runtime";
			}

			if (outcome.status == "ok" && outcome.summary.empty()) {
				outcome.summary = "Run completed";
			}

			return outcome;
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

		const auto retryPendingUntilMs = ReadRetryPendingUntilMs(job);
		if (retryPendingUntilMs.has_value() && retryPendingUntilMs.value() > nowMs) {
			return retryPendingUntilMs.value();
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
			const RunOutcome outcome = EvaluateRunOutcome(*it);
			state["runningAtMs"] = nowMs;
			state["lastRunAtMs"] = nowMs;
			state["lastStatus"] = outcome.status;
			state["lastRunStatus"] = outcome.status;
			state["lastError"] = outcome.error.empty() ? CronJson(nullptr) : CronJson(outcome.error);
			state["lastErrorCategory"] = outcome.errorCategory.empty() ? CronJson(nullptr) : CronJson(outcome.errorCategory);
			state["lastDelivered"] = outcome.delivered;
			state["lastDeliveryStatus"] = outcome.deliveryStatus;
			state["lastDeliveryError"] =
				outcome.deliveryStatus == "not-delivered"
				? CronJson(outcome.error)
				: CronJson(nullptr);
			state["lastDurationMs"] = 0;
			state["runningAtMs"] = nullptr;

			const bool deleteAfterRun = (*it).value("deleteAfterRun", false);
			const std::string jobName = (*it).value("name", std::string());
			const CronJson retry = (*it).value("retry", CronJson::object());
			const std::int64_t maxAttempts = (std::max)(
				static_cast<std::int64_t>(0),
				TryReadInt64Field(retry, "maxAttempts").value_or(0));
			const std::int64_t previousAttempt =
				TryReadInt64Field(state, "retryAttempt").value_or(0);
			const std::int64_t previousConsecutiveErrors =
				TryReadInt64Field(state, "consecutiveErrors").value_or(0);
			bool scheduledRetry = false;
			bool failureAlertTriggered = false;
			std::int64_t failureAlertAtMs = 0;
			std::int64_t consecutiveErrors = 0;
			std::int64_t retryAttempt = previousAttempt;
			std::optional<std::int64_t> nextAfterRun;
			if (outcome.status == "error" && previousAttempt < maxAttempts) {
				retryAttempt = previousAttempt + 1;
				const std::int64_t delayMs = ResolveRetryDelayMs(retry, retryAttempt);
				const std::int64_t retryAtMs = nowMs + (std::max)(static_cast<std::int64_t>(0), delayMs);
				state["retryAttempt"] = retryAttempt;
				state["retryPendingUntilMs"] = retryAtMs;
				state["nextRunAtMs"] = retryAtMs;
				nextAfterRun = retryAtMs;
				scheduledRetry = true;
			}
			else {
				state["retryAttempt"] = 0;
				state["retryPendingUntilMs"] = CronJson(nullptr);
			}

			if (outcome.status == "error") {
				consecutiveErrors = previousConsecutiveErrors + 1;
				state["consecutiveErrors"] = consecutiveErrors;

				if ((*it).contains("failureAlert") && (*it)["failureAlert"].is_boolean() && !(*it)["failureAlert"].get<bool>()) {
					state["lastFailureAlertAtMs"] = CronJson(nullptr);
				}
				else {
					const std::int64_t alertAfter = ResolveFailureAlertAfter(*it);
					const std::int64_t cooldownMs = ResolveFailureAlertCooldownMs(*it);
					const std::int64_t lastAlertAtMs =
						TryReadInt64Field(state, "lastFailureAlertAtMs").value_or(0);
					const bool cooldownOpen =
						lastAlertAtMs <= 0 || (nowMs - lastAlertAtMs) >= cooldownMs;
					if (consecutiveErrors >= alertAfter && cooldownOpen) {
						failureAlertTriggered = true;
						failureAlertAtMs = nowMs;
						state["lastFailureAlertAtMs"] = nowMs;
					}
				}
			}
			else {
				state["consecutiveErrors"] = 0;
			}

			if (!deleteAfterRun && !scheduledRetry) {
				nextAfterRun = ComputeNextRunAtMs(*it, nowMs);
				state["nextRunAtMs"] =
					nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr);
				(*it)["updatedAtMs"] = nowMs;
			}

			runs.push_back({
				{ "ts", nowMs },
				{ "jobId", id },
				{ "action", "finished" },
				{ "status", outcome.status },
				{ "summary", outcome.summary },
				{ "error", outcome.error.empty() ? CronJson(nullptr) : CronJson(outcome.error) },
				{ "errorCategory", outcome.errorCategory.empty() ? CronJson(nullptr) : CronJson(outcome.errorCategory) },
				{ "deliveryStatus", outcome.deliveryStatus },
				{ "delivered", outcome.delivered },
				{ "consecutiveErrors", consecutiveErrors },
				{ "failureAlertTriggered", failureAlertTriggered },
				{ "failureAlertAtMs", failureAlertTriggered ? CronJson(failureAlertAtMs) : CronJson(nullptr) },
				{ "retryAttempt", retryAttempt },
				{ "retryScheduled", scheduledRetry },
				{ "retryScheduledAtMs", scheduledRetry && nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr) },
				{ "durationMs", 0 },
				{ "runAtMs", nowMs },
				{ "nextRunAtMs", deleteAfterRun ? CronJson(nullptr) : (nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr)) },
				{ "jobName", jobName },
				{ "runId", BuildCronRunId(nowMs) }
			});
			++executed;

			if (deleteAfterRun && !scheduledRetry) {
				it = jobs.erase(it);
				continue;
			}

			++it;
		}

		return executed;
	}

} // namespace blazeclaw::cron
