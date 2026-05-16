#include "pch.h"

#include "CronNormalize.h"

#include <algorithm>

namespace blazeclaw::cron {

	namespace {
		CronJson& EnsureStateObject(CronJson& job) {
			if (!job.contains("state") || !job["state"].is_object()) {
				job["state"] = CronJson::object();
			}
			return job["state"];
		}

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

		std::string NormalizeScheduleKind(const std::string& value) {
			const std::string normalized = ToLowerCopy(TrimCopy(value));
			if (normalized == "at" || normalized == "every" || normalized == "cron") {
				return normalized;
			}
			return {};
		}

		CronJson NormalizeScheduleObject(const CronJson& scheduleInput) {
			if (!scheduleInput.is_object()) {
				throw std::invalid_argument("`schedule` must be an object");
			}

			CronJson schedule = scheduleInput;
			std::string kind = NormalizeScheduleKind(schedule.value("kind", std::string()));

			if (kind.empty()) {
				if (schedule.contains("everyMs")) {
					kind = "every";
				}
				else if (schedule.contains("expr") || schedule.contains("cron")) {
					kind = "cron";
				}
				else {
					kind = "at";
				}
			}
			schedule["kind"] = kind;

			if (kind == "every") {
				std::int64_t everyMs = 60'000;
				if (schedule.contains("everyMs")) {
					everyMs = (std::max)(static_cast<std::int64_t>(1000),
						ParseInt64Loose(schedule["everyMs"]).value_or(60'000));
				}
				schedule["everyMs"] = everyMs;
				schedule.erase("at");
				schedule.erase("atMs");
				schedule.erase("expr");
				schedule.erase("cron");
				return schedule;
			}

			if (kind == "cron") {
				std::string expr = TrimCopy(schedule.value("expr", std::string()));
				if (expr.empty() && schedule.contains("cron") && schedule["cron"].is_string()) {
					expr = TrimCopy(schedule["cron"].get<std::string>());
				}
				if (expr.empty()) {
					expr = "* * * * *";
				}
				schedule["expr"] = expr;
				schedule.erase("cron");

				if (schedule.contains("staggerMs")) {
					const auto staggerMs = ParseInt64Loose(schedule["staggerMs"]);
					if (staggerMs.has_value() && staggerMs.value() >= 0) {
						schedule["staggerMs"] = staggerMs.value();
					}
					else {
						schedule.erase("staggerMs");
					}
				}

				schedule.erase("at");
				schedule.erase("atMs");
				schedule.erase("everyMs");
				return schedule;
			}

			// at
			std::optional<std::int64_t> atMs;
			if (schedule.contains("atMs")) {
				atMs = ParseInt64Loose(schedule["atMs"]);
			}
			if (!atMs.has_value() && schedule.contains("at")) {
				atMs = ParseInt64Loose(schedule["at"]);
			}
			if (atMs.has_value()) {
				schedule["atMs"] = atMs.value();
			}
			schedule.erase("everyMs");
			schedule.erase("expr");
			schedule.erase("cron");
			return schedule;
		}

		CronJson NormalizePayloadObject(const CronJson& payloadInput) {
			if (!payloadInput.is_object()) {
				throw std::invalid_argument("`payload` must be an object");
			}

			CronJson payload = payloadInput;
			std::string kind = ToLowerCopy(TrimCopy(payload.value("kind", std::string())));

			const std::string message = TrimCopy(payload.value("message", std::string()));
			const std::string text = TrimCopy(payload.value("text", std::string()));

			if (kind.empty()) {
				if (!message.empty()) {
					kind = "agentturn";
				}
				else if (!text.empty()) {
					kind = "systemevent";
				}
				else {
					kind = "systemevent";
				}
			}

			if (kind == "agentturn") {
				payload["kind"] = "agentTurn";
				if (message.empty() && !text.empty()) {
					payload["message"] = text;
				}
				else if (!message.empty()) {
					payload["message"] = message;
				}
				payload.erase("text");
				return payload;
			}

			payload["kind"] = "systemEvent";
			if (text.empty() && !message.empty()) {
				payload["text"] = message;
			}
			else if (!text.empty()) {
				payload["text"] = text;
			}
			payload.erase("message");
			payload.erase("model");
			payload.erase("fallbacks");
			payload.erase("thinking");
			payload.erase("timeoutSeconds");
			payload.erase("toolsAllow");
			payload.erase("lightContext");
			payload.erase("allowUnsafeExternalContent");
			return payload;
		}

		void NormalizeDeliveryObject(CronJson& root, const char* key) {
			if (!root.contains(key) || !root[key].is_object()) {
				return;
			}

			CronJson& delivery = root[key];
			std::string mode = ToLowerCopy(TrimCopy(delivery.value("mode", std::string())));
			if (mode != "none" && mode != "announce" && mode != "webhook") {
				mode = "announce";
			}
			delivery["mode"] = mode;

			if (delivery.contains("channel") && delivery["channel"].is_string()) {
				const std::string channel = TrimCopy(delivery["channel"].get<std::string>());
				if (channel.empty()) {
					delivery.erase("channel");
				}
				else {
					delivery["channel"] = channel;
				}
			}

			if (delivery.contains("to") && delivery["to"].is_string()) {
				const std::string to = TrimCopy(delivery["to"].get<std::string>());
				if (to.empty()) {
					delivery.erase("to");
				}
				else {
					delivery["to"] = to;
				}
			}

			if (delivery.contains("accountId") && delivery["accountId"].is_string()) {
				const std::string accountId = TrimCopy(delivery["accountId"].get<std::string>());
				if (accountId.empty()) {
					delivery.erase("accountId");
				}
				else {
					delivery["accountId"] = accountId;
				}
			}
		}

		void NormalizeRetryObject(CronJson& root) {
			if (!root.contains("retry") || !root["retry"].is_object()) {
				return;
			}

			CronJson& retry = root["retry"];
			if (retry.contains("maxAttempts")) {
				const auto value = ParseInt64Loose(retry["maxAttempts"]);
				retry["maxAttempts"] = (std::max)(static_cast<std::int64_t>(0), value.value_or(0));
			}
			if (retry.contains("backoffMs") && retry["backoffMs"].is_array()) {
				CronJson normalized = CronJson::array();
				for (const auto& item : retry["backoffMs"]) {
					const auto delayMs = ParseInt64Loose(item);
					if (!delayMs.has_value() || delayMs.value() < 0) {
						continue;
					}
					normalized.push_back(delayMs.value());
				}
				retry["backoffMs"] = normalized;
			}
		}

		void NormalizeFailureAlertObject(CronJson& root) {
			if (root.contains("failureAlert") &&
				root["failureAlert"].is_boolean() &&
				!root["failureAlert"].get<bool>()) {
				return;
			}

			if (!root.contains("failureAlert") || !root["failureAlert"].is_object()) {
				return;
			}

			CronJson& failureAlert = root["failureAlert"];
			if (failureAlert.contains("after")) {
				const auto after = ParseInt64Loose(failureAlert["after"]);
				failureAlert["after"] = (std::max)(static_cast<std::int64_t>(1), after.value_or(1));
			}
			if (failureAlert.contains("cooldownMs")) {
				const auto cooldownMs = ParseInt64Loose(failureAlert["cooldownMs"]);
				failureAlert["cooldownMs"] =
					(std::max)(static_cast<std::int64_t>(0), cooldownMs.value_or(0));
			}
			if (failureAlert.contains("mode") && failureAlert["mode"].is_string()) {
				std::string mode = ToLowerCopy(TrimCopy(failureAlert["mode"].get<std::string>()));
				if (mode != "announce" && mode != "webhook") {
					mode = "announce";
				}
				failureAlert["mode"] = mode;
			}
		}
	}

	std::string CronNormalize::ResolveCronId(const CronJson& params) {
		if (!params.is_object()) {
			return {};
		}

		if (params.contains("id") && params["id"].is_string()) {
			return TrimCopy(params["id"].get<std::string>());
		}
		if (params.contains("jobId") && params["jobId"].is_string()) {
			return TrimCopy(params["jobId"].get<std::string>());
		}

		return {};
	}

	std::string CronNormalize::ResolveSessionTarget(const CronJson& params) {
		const std::string raw = TrimCopy(
			params.value("sessionTarget", std::string()));
		if (!raw.empty()) {
			const std::string lowered = ToLowerCopy(raw);
			if (lowered == "main" || lowered == "isolated") {
				return lowered;
			}
			if (lowered == "current") {
				return "isolated";
			}
			if (lowered.rfind("session:", 0) == 0) {
				return raw;
			}
		}

		if (params.contains("payload") && params["payload"].is_object()) {
			const std::string payloadKind =
				ToLowerCopy(TrimCopy(params["payload"].value("kind", std::string())));
			if (payloadKind == "agentturn") {
				return "isolated";
			}
		}

		return "main";
	}

	CronJson CronNormalize::NormalizeAddInput(const CronJson& params) {
		const std::string name = TrimCopy(params.value("name", std::string()));
		if (name.empty()) {
			throw std::invalid_argument("`name` must be a non-empty string");
		}
		if (!params.contains("schedule")) {
			throw std::invalid_argument("`schedule` must be an object");
		}
		if (!params.contains("payload")) {
			throw std::invalid_argument("`payload` must be an object");
		}

		const CronJson normalizedSchedule = NormalizeScheduleObject(params["schedule"]);
		const CronJson normalizedPayload = NormalizePayloadObject(params["payload"]);
		const std::string scheduleKind =
			ToLowerCopy(normalizedSchedule.value("kind", std::string()));

		CronJson normalized = {
			{ "name", name },
			{ "description", params.value("description", std::string()) },
			{ "enabled", params.value("enabled", true) },
			{ "schedule", normalizedSchedule },
			{ "payload", normalizedPayload },
			{ "wakeMode", NormalizeWakeMode(params.value("wakeMode", std::string(kWakeModeNow))) },
			{ "sessionTarget", ResolveSessionTarget(params) },
			{ "deleteAfterRun", params.value("deleteAfterRun", scheduleKind == "at") },
			{ "state", CronJson::object() }
		};

		if (params.contains("delivery") && params["delivery"].is_object()) {
			normalized["delivery"] = params["delivery"];
		}
		if (params.contains("agentId") && params["agentId"].is_string()) {
			const std::string agentId = TrimCopy(params["agentId"].get<std::string>());
			if (!agentId.empty()) {
				normalized["agentId"] = agentId;
			}
		}
		else if (params.contains("agentId") && params["agentId"].is_null()) {
			normalized["agentId"] = nullptr;
		}
		if (params.contains("sessionKey") && params["sessionKey"].is_string()) {
			const std::string sessionKey = TrimCopy(params["sessionKey"].get<std::string>());
			if (!sessionKey.empty()) {
				normalized["sessionKey"] = sessionKey;
			}
		}
		else if (params.contains("sessionKey") && params["sessionKey"].is_null()) {
			normalized["sessionKey"] = nullptr;
		}
		if (params.contains("retry") && params["retry"].is_object()) {
			normalized["retry"] = params["retry"];
		}
		if (params.contains("failureAlert") && params["failureAlert"].is_object()) {
			normalized["failureAlert"] = params["failureAlert"];
		}
		else if (params.contains("failureAlert") &&
			params["failureAlert"].is_boolean() &&
			!params["failureAlert"].get<bool>()) {
			normalized["failureAlert"] = false;
		}

		NormalizeDeliveryObject(normalized, "delivery");
		NormalizeRetryObject(normalized);
		NormalizeFailureAlertObject(normalized);

		return normalized;
	}

	void CronNormalize::ApplyPatch(CronJson& job, const CronJson& patch) {
		if (!patch.is_object()) {
			throw std::invalid_argument("`patch` must be an object");
		}

		if (patch.contains("name") && patch["name"].is_string()) {
			const std::string name = TrimCopy(patch["name"].get<std::string>());
			if (!name.empty()) {
				job["name"] = name;
			}
		}
		if (patch.contains("description") && patch["description"].is_string()) {
			job["description"] = patch["description"].get<std::string>();
		}
		if (patch.contains("enabled") && patch["enabled"].is_boolean()) {
			job["enabled"] = patch["enabled"].get<bool>();
		}
		if (patch.contains("schedule") && patch["schedule"].is_object()) {
			job["schedule"] = NormalizeScheduleObject(patch["schedule"]);
		}
		if (patch.contains("payload") && patch["payload"].is_object()) {
			job["payload"] = NormalizePayloadObject(patch["payload"]);
		}
		if (patch.contains("delivery") && patch["delivery"].is_object()) {
			job["delivery"] = patch["delivery"];
			NormalizeDeliveryObject(job, "delivery");
		}
		if (patch.contains("retry") && patch["retry"].is_object()) {
			job["retry"] = patch["retry"];
			NormalizeRetryObject(job);
		}
		if (patch.contains("failureAlert") && patch["failureAlert"].is_object()) {
			job["failureAlert"] = patch["failureAlert"];
			NormalizeFailureAlertObject(job);
		}
		else if (patch.contains("failureAlert") &&
			patch["failureAlert"].is_boolean() &&
			!patch["failureAlert"].get<bool>()) {
			job["failureAlert"] = false;
		}
		if (patch.contains("sessionTarget") && patch["sessionTarget"].is_string()) {
			const std::string sessionTargetRaw = TrimCopy(patch["sessionTarget"].get<std::string>());
			const std::string sessionTarget = ToLowerCopy(sessionTargetRaw);
			if (sessionTarget == "main" || sessionTarget == "isolated") {
				job["sessionTarget"] = sessionTarget;
			}
			else if (sessionTarget == "current") {
				job["sessionTarget"] = "isolated";
			}
			else if (sessionTarget.rfind("session:", 0) == 0) {
				job["sessionTarget"] = sessionTargetRaw;
			}
		}
		if (patch.contains("wakeMode") && patch["wakeMode"].is_string()) {
			const std::string wakeMode =
				NormalizeWakeMode(patch["wakeMode"].get<std::string>());
			job["wakeMode"] = wakeMode;
		}
		if (patch.contains("deleteAfterRun") && patch["deleteAfterRun"].is_boolean()) {
			job["deleteAfterRun"] = patch["deleteAfterRun"].get<bool>();
		}
		if (patch.contains("agentId") && patch["agentId"].is_string()) {
			const std::string agentId = TrimCopy(patch["agentId"].get<std::string>());
			if (agentId.empty()) {
				job.erase("agentId");
			}
			else {
				job["agentId"] = agentId;
			}
		}
		else if (patch.contains("agentId") && patch["agentId"].is_null()) {
			job.erase("agentId");
		}
		if (patch.contains("sessionKey") && patch["sessionKey"].is_string()) {
			const std::string sessionKey = TrimCopy(patch["sessionKey"].get<std::string>());
			if (sessionKey.empty()) {
				job.erase("sessionKey");
			}
			else {
				job["sessionKey"] = sessionKey;
			}
		}
		else if (patch.contains("sessionKey") && patch["sessionKey"].is_null()) {
			job.erase("sessionKey");
		}
	}

	void CronNormalize::NormalizeLoadedJob(CronJson& job) {
		if (!job.is_object()) {
			job = CronJson::object();
		}

		if (!job.contains("id") || !job["id"].is_string()) {
			job["id"] = std::string();
		}
		if (!job.contains("name") || !job["name"].is_string()) {
			job["name"] = std::string("unnamed");
		}
		if (!job.contains("description") || !job["description"].is_string()) {
			job["description"] = std::string();
		}
		if (!job.contains("enabled") || !job["enabled"].is_boolean()) {
			job["enabled"] = true;
		}
		if (!job.contains("wakeMode") || !job["wakeMode"].is_string()) {
			job["wakeMode"] = std::string(kWakeModeNow);
		}
		else {
			job["wakeMode"] = NormalizeWakeMode(job["wakeMode"].get<std::string>());
		}
		if (!job.contains("sessionTarget") || !job["sessionTarget"].is_string()) {
			job["sessionTarget"] = "main";
		}
		if (!job.contains("deleteAfterRun") || !job["deleteAfterRun"].is_boolean()) {
			job["deleteAfterRun"] = false;
		}

		if (!job.contains("schedule") || !job["schedule"].is_object()) {
			job["schedule"] = {
				{ "kind", "every" },
				{ "everyMs", 60'000 }
			};
		}
		else {
			job["schedule"] = NormalizeScheduleObject(job["schedule"]);
		}

		if (!job.contains("payload") || !job["payload"].is_object()) {
			job["payload"] = {
				{ "kind", "systemEvent" },
				{ "text", "" }
			};
		}
		else {
			job["payload"] = NormalizePayloadObject(job["payload"]);
		}

		NormalizeDeliveryObject(job, "delivery");
		NormalizeRetryObject(job);
		NormalizeFailureAlertObject(job);

		EnsureStateObject(job);
	}

} // namespace blazeclaw::cron
