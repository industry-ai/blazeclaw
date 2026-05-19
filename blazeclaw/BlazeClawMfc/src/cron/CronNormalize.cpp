#include "pch.h"

#include "CronNormalize.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::cron {

	namespace {
		constexpr std::size_t kInferredNameMaxLength = 72;

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

		bool IsLeapYear(const int year) {
			if (year % 400 == 0) {
				return true;
			}
			if (year % 100 == 0) {
				return false;
			}
			return year % 4 == 0;
		}

		int DaysInMonth(const int year, const int month) {
			switch (month) {
			case 1:
			case 3:
			case 5:
			case 7:
			case 8:
			case 10:
			case 12:
				return 31;
			case 4:
			case 6:
			case 9:
			case 11:
				return 30;
			case 2:
				return IsLeapYear(year) ? 29 : 28;
			default:
				return 0;
			}
		}

		std::int64_t DaysFromCivil(
			int year,
			unsigned month,
			unsigned day) {
			year -= month <= 2;
			const int era = (year >= 0 ? year : year - 399) / 400;
			const unsigned yoe = static_cast<unsigned>(year - era * 400);
			const unsigned doy =
				(153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
			const unsigned doe =
				yoe * 365 + yoe / 4 - yoe / 100 + doy;
			return static_cast<std::int64_t>(era) * 146097 +
				static_cast<std::int64_t>(doe) -
				719468;
		}

		std::optional<int> ParseFixedInt(
			const std::string& text,
			const std::size_t offset,
			const std::size_t count) {
			if (offset + count > text.size()) {
				return std::nullopt;
			}

			int parsed = 0;
			for (std::size_t index = 0; index < count; ++index) {
				const unsigned char ch = static_cast<unsigned char>(text[offset + index]);
				if (std::isdigit(ch) == 0) {
					return std::nullopt;
				}
				parsed = parsed * 10 + (ch - '0');
			}
			return parsed;
		}

		std::optional<std::int64_t> ParseIso8601ToUtcMs(const std::string& value) {
			const std::string text = TrimCopy(value);
			if (text.size() < 19) {
				return std::nullopt;
			}

			if (text[4] != '-' ||
				text[7] != '-' ||
				(text[10] != 'T' && text[10] != 't' && text[10] != ' ') ||
				text[13] != ':' ||
				text[16] != ':') {
				return std::nullopt;
			}

			const auto year = ParseFixedInt(text, 0, 4);
			const auto month = ParseFixedInt(text, 5, 2);
			const auto day = ParseFixedInt(text, 8, 2);
			const auto hour = ParseFixedInt(text, 11, 2);
			const auto minute = ParseFixedInt(text, 14, 2);
			const auto second = ParseFixedInt(text, 17, 2);
			if (!year.has_value() ||
				!month.has_value() ||
				!day.has_value() ||
				!hour.has_value() ||
				!minute.has_value() ||
				!second.has_value()) {
				return std::nullopt;
			}

			if (month.value() < 1 || month.value() > 12) {
				return std::nullopt;
			}
			const int maxDay = DaysInMonth(year.value(), month.value());
			if (day.value() < 1 || day.value() > maxDay) {
				return std::nullopt;
			}
			if (hour.value() < 0 || hour.value() > 23 ||
				minute.value() < 0 || minute.value() > 59 ||
				second.value() < 0 || second.value() > 59) {
				return std::nullopt;
			}

			std::size_t index = 19;
			std::int64_t millis = 0;
			if (index < text.size() && text[index] == '.') {
				++index;
				std::size_t digits = 0;
				std::int64_t fraction = 0;
				while (index < text.size()) {
					const unsigned char ch = static_cast<unsigned char>(text[index]);
					if (std::isdigit(ch) == 0) {
						break;
					}
					if (digits < 3) {
						fraction = fraction * 10 + (ch - '0');
					}
					++digits;
					++index;
				}
				if (digits == 0) {
					return std::nullopt;
				}
				if (digits == 1) {
					millis = fraction * 100;
				}
				else if (digits == 2) {
					millis = fraction * 10;
				}
				else {
					millis = fraction;
				}
			}

			int timezoneOffsetMinutes = 0;
			if (index < text.size()) {
				if ((text[index] == 'Z' || text[index] == 'z') && index + 1 == text.size()) {
					index = text.size();
				}
				else if (text[index] == '+' || text[index] == '-') {
					const int sign = text[index] == '+' ? 1 : -1;
					++index;
					const auto offsetHour = ParseFixedInt(text, index, 2);
					if (!offsetHour.has_value()) {
						return std::nullopt;
					}
					index += 2;

					if (index < text.size() && text[index] == ':') {
						++index;
					}

					const auto offsetMinute = ParseFixedInt(text, index, 2);
					if (!offsetMinute.has_value()) {
						return std::nullopt;
					}
					index += 2;
					if (index != text.size()) {
						return std::nullopt;
					}

					if (offsetHour.value() < 0 || offsetHour.value() > 23 ||
						offsetMinute.value() < 0 || offsetMinute.value() > 59) {
						return std::nullopt;
					}

					timezoneOffsetMinutes = sign *
						(offsetHour.value() * 60 + offsetMinute.value());
				}
				else {
					return std::nullopt;
				}
			}

			const std::int64_t daysSinceEpoch = DaysFromCivil(
				year.value(),
				static_cast<unsigned>(month.value()),
				static_cast<unsigned>(day.value()));
			const std::int64_t secondsSinceEpoch =
				daysSinceEpoch * 24 * 60 * 60 +
				static_cast<std::int64_t>(hour.value()) * 60 * 60 +
				static_cast<std::int64_t>(minute.value()) * 60 +
				static_cast<std::int64_t>(second.value());

			return secondsSinceEpoch * 1000 + millis -
				static_cast<std::int64_t>(timezoneOffsetMinutes) * 60 * 1000;
		}

		std::optional<std::int64_t> ResolveScheduleAtMs(const CronJson& schedule) {
			if (schedule.contains("atMs")) {
				const auto atMs = ParseInt64Loose(schedule["atMs"]);
				if (atMs.has_value()) {
					return atMs;
				}
			}

			if (!schedule.contains("at")) {
				return std::nullopt;
			}

			const auto numericAt = ParseInt64Loose(schedule["at"]);
			if (numericAt.has_value()) {
				return numericAt;
			}

			if (schedule["at"].is_string()) {
				return ParseIso8601ToUtcMs(schedule["at"].get<std::string>());
			}

			return std::nullopt;
		}

		std::string CollapseWhitespace(const std::string& value) {
			std::string collapsed;
			collapsed.reserve(value.size());
			bool previousWhitespace = false;
			for (char ch : value) {
				const bool isWhitespace = std::isspace(static_cast<unsigned char>(ch)) != 0;
				if (isWhitespace) {
					if (!previousWhitespace) {
						collapsed.push_back(' ');
					}
					previousWhitespace = true;
					continue;
				}

				collapsed.push_back(ch);
				previousWhitespace = false;
			}

			return TrimCopy(collapsed);
		}

		std::string TruncateForName(const std::string& value) {
			if (value.size() <= kInferredNameMaxLength) {
				return value;
			}

			if (kInferredNameMaxLength <= 3) {
				return value.substr(0, kInferredNameMaxLength);
			}

			return value.substr(0, kInferredNameMaxLength - 3) + "...";
		}

		std::string InferCronName(
			const CronJson& params,
			const CronJson& normalizedPayload,
			const CronJson& normalizedSchedule) {
			const std::string explicitName =
				TrimCopy(params.value("name", std::string()));
			if (!explicitName.empty()) {
				return explicitName;
			}

			std::string candidate;
			if (normalizedPayload.is_object()) {
				const std::string payloadKind = ToLowerCopy(
					TrimCopy(normalizedPayload.value("kind", std::string())));
				if (payloadKind == "agentturn") {
					candidate = TrimCopy(normalizedPayload.value("message", std::string()));
				}
				else {
					candidate = TrimCopy(normalizedPayload.value("text", std::string()));
				}
			}

			candidate = TruncateForName(CollapseWhitespace(candidate));
			if (!candidate.empty()) {
				return candidate;
			}

			const std::string scheduleKind = ToLowerCopy(
				TrimCopy(normalizedSchedule.value("kind", std::string("every"))));
			if (scheduleKind == "at") {
				return "cron-at";
			}
			if (scheduleKind == "cron") {
				return "cron-expression";
			}

			return "cron-job";
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
			const std::optional<std::int64_t> atMs = ResolveScheduleAtMs(schedule);
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
			const bool modeProvided =
				delivery.contains("mode") &&
				delivery["mode"].is_string() &&
				!TrimCopy(delivery["mode"].get<std::string>()).empty();
			std::string mode = ToLowerCopy(TrimCopy(delivery.value("mode", std::string())));
			if (mode != "none" && mode != "announce" && mode != "webhook") {
				mode = "announce";
			}

			if (mode != "webhook" &&
				!modeProvided &&
				delivery.contains("url") &&
				delivery["url"].is_string()) {
				const std::string url = TrimCopy(delivery["url"].get<std::string>());
				const std::string lowered = ToLowerCopy(url);
				if (lowered.rfind("http://", 0) == 0 ||
					lowered.rfind("https://", 0) == 0) {
					mode = "webhook";
				}
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

			if (delivery.contains("bestEffort") && !delivery["bestEffort"].is_boolean()) {
				delivery.erase("bestEffort");
			}

			if (delivery.contains("failureDestination") && delivery["failureDestination"].is_object()) {
				CronJson& failureDestination = delivery["failureDestination"];
				const bool failureModeProvided =
					failureDestination.contains("mode") &&
					failureDestination["mode"].is_string() &&
					!TrimCopy(failureDestination["mode"].get<std::string>()).empty();
				std::string failureMode = ToLowerCopy(
					TrimCopy(failureDestination.value("mode", std::string("announce"))));
				if (failureMode != "announce" && failureMode != "webhook") {
					failureMode = "announce";
				}

				if (failureMode != "webhook" &&
					!failureModeProvided &&
					failureDestination.contains("url") &&
					failureDestination["url"].is_string()) {
					const std::string url =
						TrimCopy(failureDestination["url"].get<std::string>());
					const std::string lowered = ToLowerCopy(url);
					if (lowered.rfind("http://", 0) == 0 ||
						lowered.rfind("https://", 0) == 0) {
						failureMode = "webhook";
					}
				}
				failureDestination["mode"] = failureMode;

				if (failureDestination.contains("channel") && failureDestination["channel"].is_string()) {
					const std::string channel = TrimCopy(failureDestination["channel"].get<std::string>());
					if (channel.empty()) {
						failureDestination.erase("channel");
					}
					else {
						failureDestination["channel"] = channel;
					}
				}

				if (failureDestination.contains("to") && failureDestination["to"].is_string()) {
					const std::string to = TrimCopy(failureDestination["to"].get<std::string>());
					if (to.empty()) {
						failureDestination.erase("to");
					}
					else {
						failureDestination["to"] = to;
					}
				}

				if (failureDestination.contains("accountId") && failureDestination["accountId"].is_string()) {
					const std::string accountId = TrimCopy(failureDestination["accountId"].get<std::string>());
					if (accountId.empty()) {
						failureDestination.erase("accountId");
					}
					else {
						failureDestination["accountId"] = accountId;
					}
				}
			}
		}

		bool HasFlattenedPayloadFields(const CronJson& value) {
			if (!value.is_object()) {
				return false;
			}

			return value.contains("message") ||
				value.contains("text") ||
				value.contains("model") ||
				value.contains("fallbacks") ||
				value.contains("toolsAllow") ||
				value.contains("thinking") ||
				value.contains("timeoutSeconds") ||
				value.contains("lightContext") ||
				value.contains("allowUnsafeExternalContent");
		}

		void MergeFlattenedPayloadFields(
			const CronJson& source,
			CronJson& payload) {
			auto copyIfPresent = [&](const char* fieldName) {
				if (source.contains(fieldName)) {
					payload[fieldName] = source[fieldName];
				}
			};

			copyIfPresent("message");
			copyIfPresent("text");
			copyIfPresent("model");
			copyIfPresent("fallbacks");
			copyIfPresent("toolsAllow");
			copyIfPresent("thinking");
			copyIfPresent("timeoutSeconds");
			copyIfPresent("lightContext");
			copyIfPresent("allowUnsafeExternalContent");
		}

		void StripFlattenedPayloadFields(CronJson& value) {
			value.erase("message");
			value.erase("text");
			value.erase("model");
			value.erase("fallbacks");
			value.erase("toolsAllow");
			value.erase("thinking");
			value.erase("timeoutSeconds");
			value.erase("lightContext");
			value.erase("allowUnsafeExternalContent");
		}

		CronJson BuildPayloadFromObjectCompatibility(const CronJson& value) {
			if (value.contains("payload") && value["payload"].is_object()) {
				CronJson payload = value["payload"];
				MergeFlattenedPayloadFields(value, payload);
				return payload;
			}

			if (!HasFlattenedPayloadFields(value)) {
				return CronJson::object();
			}

			CronJson payload = CronJson::object();
			MergeFlattenedPayloadFields(value, payload);
			if (!payload.contains("kind")) {
				const bool hasMessage = payload.contains("message") && payload["message"].is_string() &&
					!TrimCopy(payload["message"].get<std::string>()).empty();
				const bool hasText = payload.contains("text") && payload["text"].is_string() &&
					!TrimCopy(payload["text"].get<std::string>()).empty();
				if (hasMessage || payload.contains("model") || payload.contains("fallbacks") ||
					payload.contains("toolsAllow") || payload.contains("thinking") ||
					payload.contains("timeoutSeconds") || payload.contains("lightContext") ||
					payload.contains("allowUnsafeExternalContent")) {
					payload["kind"] = "agentTurn";
				}
				else if (hasText) {
					payload["kind"] = "systemEvent";
				}
			}

			return payload;
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
				if (params.contains("sessionKey") && params["sessionKey"].is_string()) {
					const std::string sessionKey =
						TrimCopy(params["sessionKey"].get<std::string>());
					if (!sessionKey.empty()) {
						return "session:" + sessionKey;
					}
				}
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
		if (!params.contains("schedule")) {
			throw std::invalid_argument("`schedule` must be an object");
		}

		CronJson payloadInput = BuildPayloadFromObjectCompatibility(params);
		if (!payloadInput.is_object() || payloadInput.empty()) {
			throw std::invalid_argument("`payload` must be an object");
		}

		const CronJson normalizedSchedule = NormalizeScheduleObject(params["schedule"]);
		const CronJson normalizedPayload = NormalizePayloadObject(payloadInput);
		const std::string scheduleKind =
			ToLowerCopy(normalizedSchedule.value("kind", std::string()));
		const std::string resolvedName = InferCronName(
			params,
			normalizedPayload,
			normalizedSchedule);

		CronJson normalized = {
			{ "name", resolvedName },
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
		StripFlattenedPayloadFields(normalized);

		return normalized;
	}

	CronJson CronNormalize::NormalizePatchInput(const CronJson& patch) {
		if (!patch.is_object()) {
			throw std::invalid_argument("`patch` must be an object");
		}

		CronJson normalized = patch;
		if (normalized.contains("schedule") && normalized["schedule"].is_object()) {
			normalized["schedule"] = NormalizeScheduleObject(normalized["schedule"]);
		}

		const CronJson payloadCompatibility = BuildPayloadFromObjectCompatibility(normalized);
		if (payloadCompatibility.is_object() && !payloadCompatibility.empty()) {
			normalized["payload"] = NormalizePayloadObject(payloadCompatibility);
		}

		if (normalized.contains("delivery") && normalized["delivery"].is_object()) {
			NormalizeDeliveryObject(normalized, "delivery");
		}
		if (normalized.contains("retry") && normalized["retry"].is_object()) {
			NormalizeRetryObject(normalized);
		}
		if (normalized.contains("failureAlert") && normalized["failureAlert"].is_object()) {
			NormalizeFailureAlertObject(normalized);
		}

		StripFlattenedPayloadFields(normalized);

		return normalized;
	}

	void CronNormalize::ApplyPatch(CronJson& job, const CronJson& patch) {
		const CronJson normalizedPatch = NormalizePatchInput(patch);

		if (normalizedPatch.contains("name") && normalizedPatch["name"].is_string()) {
			const std::string name = TrimCopy(normalizedPatch["name"].get<std::string>());
			if (!name.empty()) {
				job["name"] = name;
			}
		}
		if (normalizedPatch.contains("description") && normalizedPatch["description"].is_string()) {
			job["description"] = normalizedPatch["description"].get<std::string>();
		}
		if (normalizedPatch.contains("enabled") && normalizedPatch["enabled"].is_boolean()) {
			job["enabled"] = normalizedPatch["enabled"].get<bool>();
		}
		if (normalizedPatch.contains("schedule") && normalizedPatch["schedule"].is_object()) {
			job["schedule"] = normalizedPatch["schedule"];
		}
		if (normalizedPatch.contains("payload") && normalizedPatch["payload"].is_object()) {
			job["payload"] = normalizedPatch["payload"];
		}
		if (normalizedPatch.contains("delivery") && normalizedPatch["delivery"].is_object()) {
			job["delivery"] = normalizedPatch["delivery"];
			NormalizeDeliveryObject(job, "delivery");
		}
		if (normalizedPatch.contains("retry") && normalizedPatch["retry"].is_object()) {
			job["retry"] = normalizedPatch["retry"];
			NormalizeRetryObject(job);
		}
		if (normalizedPatch.contains("failureAlert") && normalizedPatch["failureAlert"].is_object()) {
			job["failureAlert"] = normalizedPatch["failureAlert"];
			NormalizeFailureAlertObject(job);
		}
		else if (normalizedPatch.contains("failureAlert") &&
			normalizedPatch["failureAlert"].is_boolean() &&
			!normalizedPatch["failureAlert"].get<bool>()) {
			job["failureAlert"] = false;
		}
		if (normalizedPatch.contains("sessionTarget") && normalizedPatch["sessionTarget"].is_string()) {
			const std::string sessionTargetRaw = TrimCopy(normalizedPatch["sessionTarget"].get<std::string>());
			const std::string sessionTarget = ToLowerCopy(sessionTargetRaw);
			if (sessionTarget == "main" || sessionTarget == "isolated") {
				job["sessionTarget"] = sessionTarget;
			}
			else if (sessionTarget == "current") {
				std::string resolvedSessionKey;
				if (normalizedPatch.contains("sessionKey") && normalizedPatch["sessionKey"].is_string()) {
					resolvedSessionKey = TrimCopy(normalizedPatch["sessionKey"].get<std::string>());
				}
				if (resolvedSessionKey.empty() &&
					job.contains("sessionKey") &&
					job["sessionKey"].is_string()) {
					resolvedSessionKey = TrimCopy(job["sessionKey"].get<std::string>());
				}

				if (!resolvedSessionKey.empty()) {
					job["sessionTarget"] = "session:" + resolvedSessionKey;
				}
				else {
					job["sessionTarget"] = "isolated";
				}
			}
			else if (sessionTarget.rfind("session:", 0) == 0) {
				job["sessionTarget"] = sessionTargetRaw;
				const std::string embeddedSessionKey =
					TrimCopy(sessionTargetRaw.substr(std::string("session:").size()));
				if (!embeddedSessionKey.empty() &&
					(!job.contains("sessionKey") || !job["sessionKey"].is_string())) {
					job["sessionKey"] = embeddedSessionKey;
				}
			}
		}
		if (normalizedPatch.contains("wakeMode") && normalizedPatch["wakeMode"].is_string()) {
			const std::string wakeMode =
				NormalizeWakeMode(normalizedPatch["wakeMode"].get<std::string>());
			job["wakeMode"] = wakeMode;
		}
		if (normalizedPatch.contains("deleteAfterRun") && normalizedPatch["deleteAfterRun"].is_boolean()) {
			job["deleteAfterRun"] = normalizedPatch["deleteAfterRun"].get<bool>();
		}
		if (normalizedPatch.contains("agentId") && normalizedPatch["agentId"].is_string()) {
			const std::string agentId = TrimCopy(normalizedPatch["agentId"].get<std::string>());
			if (agentId.empty()) {
				job.erase("agentId");
			}
			else {
				job["agentId"] = agentId;
			}
		}
		else if (normalizedPatch.contains("agentId") && normalizedPatch["agentId"].is_null()) {
			job.erase("agentId");
		}
		if (normalizedPatch.contains("sessionKey") && normalizedPatch["sessionKey"].is_string()) {
			const std::string sessionKey = TrimCopy(normalizedPatch["sessionKey"].get<std::string>());
			if (sessionKey.empty()) {
				job.erase("sessionKey");
				if (job.contains("sessionTarget") &&
					job["sessionTarget"].is_string() &&
					ToLowerCopy(TrimCopy(job["sessionTarget"].get<std::string>())).rfind("session:", 0) == 0) {
					const std::string payloadKind =
						job.contains("payload") && job["payload"].is_object()
						? ToLowerCopy(TrimCopy(job["payload"].value("kind", std::string())))
						: std::string();
					job["sessionTarget"] = payloadKind == "agentturn"
						? CronJson("isolated")
						: CronJson("main");
				}
			}
			else {
				job["sessionKey"] = sessionKey;
				if (job.contains("sessionTarget") &&
					job["sessionTarget"].is_string() &&
					ToLowerCopy(TrimCopy(job["sessionTarget"].get<std::string>())) == "current") {
					job["sessionTarget"] = "session:" + sessionKey;
				}
			}
		}
		else if (normalizedPatch.contains("sessionKey") && normalizedPatch["sessionKey"].is_null()) {
			job.erase("sessionKey");
			if (job.contains("sessionTarget") &&
				job["sessionTarget"].is_string() &&
				ToLowerCopy(TrimCopy(job["sessionTarget"].get<std::string>())).rfind("session:", 0) == 0) {
				const std::string payloadKind =
					job.contains("payload") && job["payload"].is_object()
					? ToLowerCopy(TrimCopy(job["payload"].value("kind", std::string())))
					: std::string();
				job["sessionTarget"] = payloadKind == "agentturn"
					? CronJson("isolated")
					: CronJson("main");
			}
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
		job["sessionTarget"] = ResolveSessionTarget(job);
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
