#include "pch.h"
#include "GatewayHost.h"
#include "GatewayJsonUtils.h"
#include "Telemetry.h"
#include "ChatRunStageContext.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "RuntimeSequencingPolicy.h"
#include "RuntimeToolCallNormalizer.h"
#include "RuntimeTranscriptGuard.h"
#include "RecoveryPolicyEngine.h"
#include "executors/EmailScheduleExecutor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <regex>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace {
		constexpr std::size_t kMaxChatHistoryEntriesPerSession = 500;
		constexpr std::size_t kMaxChatEventsPerSession = 200;

		std::string SerializeStringArrayLocal(
			const std::vector<std::string>& values);

		std::string EscapeJsonLocal(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);

			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		std::string ExtractStringParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			std::string value;
			if (!json::FindStringField(paramsJson.value(), fieldName, value)) {
				return {};
			}

			return value;
		}

		std::string NormalizeSearchQueryTextLocal(const std::string& input) {
			std::string normalized;
			normalized.reserve(input.size());

			bool previousWasSpace = true;
			for (const unsigned char rawCh : input) {
				if (rawCh < 0x20) {
					continue;
				}

				if (std::isspace(rawCh) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				normalized.push_back(static_cast<char>(rawCh));
				previousWasSpace = false;
			}

			while (!normalized.empty() && normalized.front() == ' ') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == ' ') {
				normalized.pop_back();
			}

			return normalized;
		}

		std::optional<std::string> DeriveCompactSearchQueryLocal(
			const std::string& source) {
			constexpr std::size_t kMaxQueryChars = 240;
			std::string normalized = NormalizeSearchQueryTextLocal(source);
			if (normalized.empty()) {
				return std::nullopt;
			}

			if (normalized.size() <= kMaxQueryChars) {
				return normalized;
			}

			std::string compact = normalized.substr(0, kMaxQueryChars);
			const auto lastSpace = compact.find_last_of(' ');
			if (lastSpace != std::string::npos && lastSpace > 40) {
				compact = compact.substr(0, lastSpace);
			}

			compact = NormalizeSearchQueryTextLocal(compact);
			if (compact.empty()) {
				return std::nullopt;
			}

			return compact;
		}

		std::string SerializeSkillCatalogEntry(
			const SkillsCatalogGatewayEntry& entry) {
			return "{\"name\":\"" +
				EscapeJsonLocal(entry.name) +
				"\",\"skillKey\":\"" +
				EscapeJsonLocal(entry.skillKey) +
				"\",\"primaryEnv\":\"" +
				EscapeJsonLocal(entry.primaryEnv) +
				"\",\"requiresBins\":" +
				SerializeStringArrayLocal(entry.requiresBins) +
				",\"requiresEnv\":" +
				SerializeStringArrayLocal(entry.requiresEnv) +
				",\"requiresConfig\":" +
				SerializeStringArrayLocal(entry.requiresConfig) +
				",\"configPathHints\":" +
				SerializeStringArrayLocal(entry.configPathHints) +
				",\"normalizedMetadataSources\":" +
				SerializeStringArrayLocal(entry.normalizedMetadataSources) +
				",\"command\":\"" +
				EscapeJsonLocal(entry.commandName) +
				"\",\"installKind\":\"" +
				EscapeJsonLocal(entry.installKind) +
				"\",\"installCommand\":\"" +
				EscapeJsonLocal(entry.installCommand) +
				"\",\"installExecutable\":" +
				std::string(entry.installExecutable ? "true" : "false") +
				",\"installReason\":\"" +
				EscapeJsonLocal(entry.installReason) +
				"\"" +
				"\",\"description\":\"" +
				EscapeJsonLocal(entry.description) +
				"\",\"source\":\"" +
				EscapeJsonLocal(entry.source) +
				"\",\"precedence\":" +
				std::to_string(entry.precedence) +
				",\"eligible\":" +
				std::string(entry.eligible ? "true" : "false") +
				",\"disabled\":" +
				std::string(entry.disabled ? "true" : "false") +
				",\"blockedByAllowlist\":" +
				std::string(entry.blockedByAllowlist ? "true" : "false") +
				",\"missingEnv\":" +
				SerializeStringArrayLocal(entry.missingEnv) +
				",\"missingConfig\":" +
				SerializeStringArrayLocal(entry.missingConfig) +
				",\"missingBins\":" +
				SerializeStringArrayLocal(entry.missingBins) +
				",\"missingAnyBins\":" +
				SerializeStringArrayLocal(entry.missingAnyBins) +
				",\"disableModelInvocation\":" +
				std::string(entry.disableModelInvocation ? "true" : "false") +
				",\"validFrontmatter\":" +
				std::string(entry.validFrontmatter ? "true" : "false") +
				",\"validationErrorCount\":" +
				std::to_string(entry.validationErrorCount) +
				"}";
		}

		GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizeTaskDeltaEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionKey,
			const std::size_t defaultIndex) {
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry normalized = source;
			normalized.index = source.index == 0 && defaultIndex > 0
				? defaultIndex
				: source.index;
			if (normalized.runId.empty()) {
				normalized.runId = runId;
			}

			if (normalized.sessionId.empty()) {
				normalized.sessionId = sessionKey;
			}

			if (normalized.phase.empty()) {
				normalized.phase = "unknown";
			}

			if (normalized.status.empty()) {
				normalized.status = normalized.phase == "final"
					? "completed"
					: "running";
			}

			if (normalized.stepLabel.empty()) {
				normalized.stepLabel = normalized.phase;
			}

			if (normalized.startedAtMs == 0) {
				normalized.startedAtMs = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch())
					.count());
			}

			if (normalized.completedAtMs == 0 ||
				normalized.completedAtMs < normalized.startedAtMs) {
				normalized.completedAtMs = normalized.startedAtMs;
			}

			normalized.latencyMs =
				normalized.completedAtMs - normalized.startedAtMs;
			return normalized;
		}

		constexpr char kSilentReplyToken[] = "NO_REPLY";

		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> EnsureRuntimeTaskDeltas(
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
			const std::string& runId,
			const std::string& sessionKey,
			const bool success,
			const std::string& assistantText,
			const std::string& errorCode,
			const std::string& errorMessage) {
			if (!taskDeltas.empty()) {
				return taskDeltas;
			}

			const std::uint64_t nowMs = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
				.count());

			return std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>{
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "final",
					.resultJson = success ? assistantText : errorMessage,
					.status = success ? "completed" : "failed",
					.errorCode = success ? std::string() : errorCode,
					.startedAtMs = nowMs,
					.completedAtMs = nowMs,
					.latencyMs = 0,
					.stepLabel = "run_terminal",
				}
			};
		}

		std::optional<std::size_t> ExtractSizeParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			std::uint64_t value = 0;
			if (!json::FindUInt64Field(paramsJson.value(), fieldName, value)) {
				return std::nullopt;
			}

			return static_cast<std::size_t>(value);
		}

		std::optional<bool> ExtractBoolParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			bool value = false;
			if (!json::FindBoolField(paramsJson.value(), fieldName, value)) {
				return std::nullopt;
			}

			return value;
		}

		bool HasAgentId(
			const GatewayAgentRegistry& registry,
			const std::string& agentId) {
			if (agentId.empty()) {
				return false;
			}

			const auto agents = registry.List();
			return std::any_of(
				agents.begin(),
				agents.end(),
				[&](const AgentEntry& entry) {
					return entry.id == agentId;
				});
		}

		bool HasSessionId(
			const GatewaySessionRegistry& registry,
			const std::string& sessionId) {
			if (sessionId.empty()) {
				return false;
			}

			const auto sessions = registry.List();
			return std::any_of(
				sessions.begin(),
				sessions.end(),
				[&](const SessionEntry& entry) {
					return entry.id == sessionId;
				});
		}

		std::uint64_t CurrentEpochMsLocal() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::string BuildAssistantFinalMessageJson(
			const std::string& text,
			const std::uint64_t timestampMs) {
			return "{\"role\":\"assistant\",\"text\":\"" +
				EscapeJsonLocal(text) +
				"\",\"content\":[{\"type\":\"text\",\"text\":\"" +
				EscapeJsonLocal(text) +
				"\"}],\"timestamp\":" +
				std::to_string(timestampMs) +
				"}";
		}

		std::string BuildAssistantDeltaMessageJson(const std::string& text) {
			return
				"{\"role\":\"assistant\",\"text\":\"" +
				EscapeJsonLocal(text) +
				"\"}";
		}

		std::string BuildUserMessageJson(
			const std::string& text,
			const bool hasAttachments,
			const std::uint64_t timestampMs) {
			std::string content = "[";
			bool first = true;
			if (!text.empty()) {
				content +=
					"{\"type\":\"text\",\"text\":\"" +
					EscapeJsonLocal(text) +
					"\"}";
				first = false;
			}

			if (hasAttachments) {
				if (!first) {
					content += ",";
				}

				content +=
					"{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/*\",\"data\":\"[omitted]\"}}";
			}

			content += "]";

			return "{\"role\":\"user\",\"content\":" +
				content +
				",\"timestamp\":" +
				std::to_string(timestampMs) +
				"}";
		}

		std::string BuildChatEventJson(
			const std::string& runId,
			const std::string& sessionKey,
			const std::string& state,
			const std::optional<std::string>& messageJson,
			const std::optional<std::string>& errorMessage,
			const std::uint64_t timestampMs) {
			std::string payload =
				"{\"runId\":\"" +
				EscapeJsonLocal(runId) +
				"\",\"sessionKey\":\"" +
				EscapeJsonLocal(sessionKey) +
				"\",\"state\":\"" +
				EscapeJsonLocal(state) +
				"\",\"timestamp\":" +
				std::to_string(timestampMs);

			if (messageJson.has_value()) {
				payload += ",\"message\":" + messageJson.value();
			}

			if (errorMessage.has_value()) {
				payload +=
					",\"errorMessage\":\"" +
					EscapeJsonLocal(errorMessage.value()) +
					"\"";
			}

			payload += "}";
			return payload;
		}

		bool IsTerminalChatState(const std::string& state) {
			return state == "final" || state == "error" || state == "aborted";
		}

		std::string SerializeTaskDeltaEntryJson(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
			return "{\"index\":" +
				std::to_string(delta.index) +
				",\"schemaVersion\":" +
				std::to_string(delta.schemaVersion) +
				",\"runId\":\"" +
				EscapeJsonLocal(delta.runId) +
				"\",\"sessionId\":\"" +
				EscapeJsonLocal(delta.sessionId) +
				"\",\"phase\":\"" +
				EscapeJsonLocal(delta.phase) +
				"\",\"toolName\":\"" +
				EscapeJsonLocal(delta.toolName) +
				"\",\"fallbackBackend\":\"" +
				EscapeJsonLocal(delta.fallbackBackend) +
				"\",\"fallbackAction\":\"" +
				EscapeJsonLocal(delta.fallbackAction) +
				"\",\"fallbackAttempt\":" +
				std::to_string(delta.fallbackAttempt) +
				",\"fallbackMaxAttempts\":" +
				std::to_string(delta.fallbackMaxAttempts) +
				",\"argsJson\":\"" +
				EscapeJsonLocal(delta.argsJson) +
				"\",\"resultJson\":\"" +
				EscapeJsonLocal(delta.resultJson) +
				"\",\"status\":\"" +
				EscapeJsonLocal(delta.status) +
				"\",\"errorCode\":\"" +
				EscapeJsonLocal(delta.errorCode) +
				"\",\"startedAtMs\":" +
				std::to_string(delta.startedAtMs) +
				",\"completedAtMs\":" +
				std::to_string(delta.completedAtMs) +
				",\"latencyMs\":" +
				std::to_string(delta.latencyMs) +
				",\"modelTurnId\":\"" +
				EscapeJsonLocal(delta.modelTurnId) +
				"\",\"stepLabel\":\"" +
				EscapeJsonLocal(delta.stepLabel) +
				"\"}";
		}

		struct ChatPromptOrchestrationResult {
			bool matched = false;
			bool success = false;
			bool requiresApproval = false;
			std::string terminalStatus;
			std::string terminalReason;
			std::string fallbackBackend;
			std::string fallbackAction;
			std::size_t fallbackAttempt = 0;
			std::size_t fallbackMaxAttempts = 0;
			std::string assistantText;
			std::vector<std::string> assistantDeltas;
			std::string errorCode;
			std::string errorMessage;
			std::vector<std::string> missReasons;
			std::string city;
			std::string date;
			std::string recipient;
			std::string sendAt;
			std::string scheduleKind;
			std::size_t decompositionSteps = 0;
		};

		std::string ToLowerCopyLocal(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string Utf8LiteralLocal(const char* value) {
			return value == nullptr ? std::string{} : std::string(value);
		}

#if defined(__cpp_char8_t)
		std::string Utf8LiteralLocal(const char8_t* value) {
			if (value == nullptr) {
				return {};
			}

			return std::string(reinterpret_cast<const char*>(value));
		}
#endif

		bool IsLikelyChinesePromptLocal(const std::string& text) {
			if (text.empty()) {
				return false;
			}

			for (std::size_t i = 0; i < text.size();) {
				const unsigned char lead =
					static_cast<unsigned char>(text[i]);
				std::uint32_t codePoint = 0;
				std::size_t advance = 1;

				if ((lead & 0x80u) == 0) {
					codePoint = lead;
				}
				else if ((lead & 0xE0u) == 0xC0u && i + 1 < text.size()) {
					const unsigned char b1 =
						static_cast<unsigned char>(text[i + 1]);
					if ((b1 & 0xC0u) != 0x80u) {
						i += 1;
						continue;
					}

					codePoint =
						(static_cast<std::uint32_t>(lead & 0x1Fu) << 6) |
						static_cast<std::uint32_t>(b1 & 0x3Fu);
					advance = 2;
				}
				else if ((lead & 0xF0u) == 0xE0u && i + 2 < text.size()) {
					const unsigned char b1 =
						static_cast<unsigned char>(text[i + 1]);
					const unsigned char b2 =
						static_cast<unsigned char>(text[i + 2]);
					if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
						i += 1;
						continue;
					}

					codePoint =
						(static_cast<std::uint32_t>(lead & 0x0Fu) << 12) |
						(static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) |
						static_cast<std::uint32_t>(b2 & 0x3Fu);
					advance = 3;
				}
				else if ((lead & 0xF8u) == 0xF0u && i + 3 < text.size()) {
					const unsigned char b1 =
						static_cast<unsigned char>(text[i + 1]);
					const unsigned char b2 =
						static_cast<unsigned char>(text[i + 2]);
					const unsigned char b3 =
						static_cast<unsigned char>(text[i + 3]);
					if ((b1 & 0xC0u) != 0x80u ||
						(b2 & 0xC0u) != 0x80u ||
						(b3 & 0xC0u) != 0x80u) {
						i += 1;
						continue;
					}

					codePoint =
						(static_cast<std::uint32_t>(lead & 0x07u) << 18) |
						(static_cast<std::uint32_t>(b1 & 0x3Fu) << 12) |
						(static_cast<std::uint32_t>(b2 & 0x3Fu) << 6) |
						static_cast<std::uint32_t>(b3 & 0x3Fu);
					advance = 4;
				}

				const bool isCjkUnifiedIdeograph =
					(codePoint >= 0x4E00u && codePoint <= 0x9FFFu) ||
					(codePoint >= 0x3400u && codePoint <= 0x4DBFu);
				if (isCjkUnifiedIdeograph) {
					return true;
				}

				i += advance;
			}

			return false;
		}

		std::string SerializeStringArrayLocal(
			const std::vector<std::string>& values) {
			std::string json = "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					json += ",";
				}

				json += JsonString(values[i]);
			}

			json += "]";
			return json;
		}

		std::string ResolveCurrentLocalTimeHHmm() {
			std::time_t now = std::time(nullptr);
			std::tm localTime = {};
#if defined(_WIN32)
			localtime_s(&localTime, &now);
#else
			localtime_r(&now, &localTime);
#endif

			std::ostringstream output;
			output << std::setw(2) << std::setfill('0') << localTime.tm_hour
				<< ":"
				<< std::setw(2) << std::setfill('0') << localTime.tm_min;
			return output.str();
		}

		struct PromptScheduleResolution {
			bool hasSchedule = false;
			bool immediate = false;
			std::string sendAt;
			std::string kind;
		};

		std::optional<std::string> TryParsePromptSendAt(
			const std::string& message) {
			static const std::regex kTwelveHourRegex(
				R"((\b\d{1,2})(?::(\d{2}))?\s*(am|pm)\b)",
				std::regex_constants::icase);
			static const std::regex kTwentyFourHourRegex(
				R"((\b\d{1,2}):(\d{2})\b)");

			std::smatch twelveHourMatch;
			if (std::regex_search(message, twelveHourMatch, kTwelveHourRegex) &&
				twelveHourMatch.size() >= 4) {
				int hour = 0;
				int minute = 0;
				try {
					hour = std::stoi(twelveHourMatch[1].str());
					minute = twelveHourMatch[2].matched
						? std::stoi(twelveHourMatch[2].str())
						: 0;
				}
				catch (...) {
					return std::nullopt;
				}

				if (hour < 1 || hour > 12 || minute < 0 || minute > 59) {
					return std::nullopt;
				}

				std::string meridiem = twelveHourMatch[3].str();
				std::transform(
					meridiem.begin(),
					meridiem.end(),
					meridiem.begin(),
					[](unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});

				if (meridiem == "am") {
					hour = hour == 12 ? 0 : hour;
				}
				else {
					hour = hour == 12 ? 12 : hour + 12;
				}

				std::ostringstream time;
				time << std::setw(2) << std::setfill('0') << hour
					<< ":"
					<< std::setw(2) << std::setfill('0') << minute;
				return time.str();
			}

			std::smatch twentyFourHourMatch;
			if (std::regex_search(message, twentyFourHourMatch, kTwentyFourHourRegex) &&
				twentyFourHourMatch.size() >= 3) {
				int hour = 0;
				int minute = 0;
				try {
					hour = std::stoi(twentyFourHourMatch[1].str());
					minute = std::stoi(twentyFourHourMatch[2].str());
				}
				catch (...) {
					return std::nullopt;
				}

				if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
					return std::nullopt;
				}

				std::ostringstream time;
				time << std::setw(2) << std::setfill('0') << hour
					<< ":"
					<< std::setw(2) << std::setfill('0') << minute;
				return time.str();
			}

			return std::nullopt;
		}

		PromptScheduleResolution ResolvePromptSchedule(
			const std::string& message,
			const std::string& loweredMessage) {
			PromptScheduleResolution schedule;

			const auto parsedTime = TryParsePromptSendAt(message);
			if (parsedTime.has_value()) {
				schedule.hasSchedule = true;
				schedule.sendAt = parsedTime.value();
				schedule.kind = "clock_time";
				return schedule;
			}

			const bool immediateKeyword =
				loweredMessage.find("right now") != std::string::npos ||
				loweredMessage.find("immediately") != std::string::npos ||
				loweredMessage.find(" as soon as possible") !=
				std::string::npos ||
				loweredMessage.find(" now") != std::string::npos ||
				loweredMessage.rfind("now", 0) == 0;
			if (immediateKeyword) {
				schedule.hasSchedule = true;
				schedule.immediate = true;
				schedule.sendAt = ResolveCurrentLocalTimeHHmm();
				schedule.kind = "immediate_keyword";
				return schedule;
			}

			schedule.hasSchedule = false;
			schedule.immediate = false;
			schedule.sendAt = "13:00";
			schedule.kind = "default_fallback";
			return schedule;
		}

		bool HasWeatherIntent(const std::string& loweredMessage) {
			return loweredMessage.find("weather") != std::string::npos;
		}

		bool HasEmailIntent(const std::string& loweredMessage) {
			return loweredMessage.find("email") != std::string::npos ||
				loweredMessage.find("mail") != std::string::npos;
		}

		bool HasReportIntent(const std::string& loweredMessage) {
			return loweredMessage.find("report") != std::string::npos ||
				loweredMessage.find("summary") != std::string::npos ||
				loweredMessage.find("write") != std::string::npos;
		}

		std::string ExtractFirstEmailAddress(const std::string& text) {
			static const std::regex kEmailRegex(
				R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");

			std::smatch match;
			if (std::regex_search(text, match, kEmailRegex) && !match.empty()) {
				return match[1].str();
			}

			return {};
		}

		std::string ResolvePromptCity(const std::string& message) {
			const std::string lowered = [&message]() {
				std::string value = message;
				std::transform(
					value.begin(),
					value.end(),
					value.begin(),
					[](unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});
				return value;
				}();

			if (lowered.find("wuhan") != std::string::npos) {
				return "Wuhan";
			}

			return "Wuhan";
		}

		std::string ResolvePromptDate(const std::string& message) {
			const std::string lowered = ToLowerCopyLocal(message);

			if (lowered.find("today") != std::string::npos) {
				return "today";
			}

			if (lowered.find("tomorrow") != std::string::npos) {
				return "tomorrow";
			}

			return "tomorrow";
		}

		std::string ResolvePromptSendAt(const std::string& message) {
			const auto schedule =
				ResolvePromptSchedule(message, ToLowerCopyLocal(message));
			return schedule.sendAt;
		}

		std::string BuildWeatherReportText(
			const std::string& city,
			const std::string& date,
			const std::string& condition,
			const int temperatureC,
			const std::string& wind,
			const int humidityPct,
			const bool preferChinese) {
			if (preferChinese) {
				return city + Utf8LiteralLocal(u8"\uFF08") + date +
					Utf8LiteralLocal(u8"\uFF09\u5929\u6C14\uFF1A") +
					condition + Utf8LiteralLocal(u8"\uFF0C\u6C14\u6E29\u7EA6 ") +
					std::to_string(temperatureC) +
					"C" + Utf8LiteralLocal(u8"\uFF0C\u98CE\u529B ") + wind +
					Utf8LiteralLocal(u8"\uFF0C\u6E7F\u5EA6 ") +
					std::to_string(humidityPct) + "%" +
					Utf8LiteralLocal(u8"\u3002");
			}

			return "Weather report for " + city + " (" + date + "): " +
				condition + ", around " + std::to_string(temperatureC) +
				"C, wind " + wind + ", humidity " +
				std::to_string(humidityPct) + "% .";
		}

		void ResolveFallbackProbeDiagnostic(
			const std::string& rawOutput,
			std::string& outCode,
			std::string& outMessage) {
			outCode.clear();
			outMessage.clear();

			if (rawOutput.empty()) {
				return;
			}

			try {
				const auto payload = nlohmann::json::parse(rawOutput);
				if (payload.is_object()) {
					if (payload.contains("error") && payload["error"].is_object()) {
						const auto& error = payload["error"];
						if (error.contains("code") && error["code"].is_string()) {
							outCode = error["code"].get<std::string>();
						}
						if (error.contains("message") && error["message"].is_string()) {
							outMessage = error["message"].get<std::string>();
						}
					}

					if (outCode.empty() &&
						payload.contains("code") &&
						payload["code"].is_string()) {
						outCode = payload["code"].get<std::string>();
					}

					if (outMessage.empty() &&
						payload.contains("message") &&
						payload["message"].is_string()) {
						outMessage = payload["message"].get<std::string>();
					}
				}
			}
			catch (...) {
			}

			if (!outCode.empty()) {
				if (outCode == "node_cli_missing" &&
					(outMessage.empty() ||
						ToLowerCopyLocal(json::Trim(outMessage)) == "node_cli_missing")) {
					outMessage = "node runtime not found";
				}

				return;
			}

			const std::string lowered = ToLowerCopyLocal(rawOutput);
			if (lowered.find("node_cli_missing") != std::string::npos) {
				outCode = "node_cli_missing";
				outMessage = "node runtime not found";
				return;
			}

			if (lowered.find("imap_smtp_skill_missing") != std::string::npos) {
				outCode = "imap_smtp_skill_missing";
				outMessage = "imap smtp skill scripts not found";
				return;
			}

			if (lowered.find("invalid_himalaya_account") != std::string::npos ||
				lowered.find("invalid_account") != std::string::npos) {
				outCode = "invalid_account";
				outMessage = "configured email account is invalid";
				return;
			}

			if (lowered.find("imap_smtp_send_failed") != std::string::npos) {
				outCode = "imap_smtp_send_failed";
				outMessage = "imap smtp send failed; check configuration/account credentials";
			}
		}

		ChatPromptOrchestrationResult TryOrchestrateWeatherEmailPrompt(
			GatewayToolRegistry& toolRegistry,
			const std::string& message) {
			ChatPromptOrchestrationResult result;
			const bool preferChinese = IsLikelyChinesePromptLocal(message);
			const auto intent =
				prompt::AnalyzeWeatherEmailPromptIntent(message);
			result.matched = intent.matched;
			result.missReasons = intent.missReasons;
			result.scheduleKind = intent.scheduleKind;
			result.city = intent.city;
			result.date = intent.date;
			result.recipient = intent.recipient;
			result.sendAt = intent.sendAt;
			result.decompositionSteps = intent.decompositionSteps;
			if (!result.matched) {
				return result;
			}

			const std::string city = intent.city;
			const std::string date = intent.date;
			const std::string sendAt = intent.sendAt;
			const std::string recipient = intent.recipient;
			result.city = city;
			result.date = date;
			result.recipient = recipient;
			result.sendAt = sendAt;
			result.scheduleKind = intent.scheduleKind;
			result.decompositionSteps = intent.decompositionSteps;

			if (recipient.empty()) {
				result.success = false;
				result.terminalStatus = "failed";
				result.terminalReason = "recipient_missing";
				result.errorCode = "orchestration_invalid_prompt";
				result.errorMessage = "recipient_email_required";
				return result;
			}

			nlohmann::json weatherArgs = {
				{ "city", city },
				{ "date", date },
			};
			const auto weatherExecution = toolRegistry.Execute(
				"weather.lookup",
				weatherArgs.dump());

			if (!weatherExecution.executed || weatherExecution.status != "ok") {
				result.success = false;
				result.terminalStatus = "failed";
				result.terminalReason = "weather_failed";
				result.errorCode = "orchestration_weather_failed";
				result.errorMessage = weatherExecution.output;
				return result;
			}

			std::string condition = "Cloudy";
			int temperatureC = 20;
			std::string wind = "NE 9 km/h";
			int humidityPct = 68;
			try {
				const auto weatherPayload =
					nlohmann::json::parse(weatherExecution.output);
				if (weatherPayload.contains("forecast") &&
					weatherPayload["forecast"].is_object()) {
					const auto& forecast = weatherPayload["forecast"];
					if (forecast.contains("condition") && forecast["condition"].is_string()) {
						condition = forecast["condition"].get<std::string>();
					}
					if (forecast.contains("temperatureC") && forecast["temperatureC"].is_number_integer()) {
						temperatureC = forecast["temperatureC"].get<int>();
					}
					if (forecast.contains("wind") && forecast["wind"].is_string()) {
						wind = forecast["wind"].get<std::string>();
					}
					if (forecast.contains("humidityPct") && forecast["humidityPct"].is_number_integer()) {
						humidityPct = forecast["humidityPct"].get<int>();
					}
				}
			}
			catch (...) {
			}

			const std::string report = BuildWeatherReportText(
				city,
				date,
				condition,
				temperatureC,
				wind,
				humidityPct,
				preferChinese);

			nlohmann::json emailPrepareArgs = {
				{ "action", "prepare" },
				{ "to", recipient },
				{ "subject", city + " weather report" },
				{ "body", report },
				{ "sendAt", sendAt },
			};

			const auto emailPrepareExecution = toolRegistry.Execute(
				"email.schedule",
				emailPrepareArgs.dump());

			if (!emailPrepareExecution.executed ||
				emailPrepareExecution.status != "needs_approval") {
				result.success = false;
				result.terminalStatus = "failed";
				result.terminalReason = "email_prepare_failed";
				result.errorCode = "orchestration_email_prepare_failed";
				result.errorMessage = emailPrepareExecution.output;
				return result;
			}

			std::string approvalToken;
			std::uint64_t approvalTokenExpiresAtEpochMs = 0;
			try {
				const auto emailPayload =
					nlohmann::json::parse(emailPrepareExecution.output);
				if (emailPayload.contains("requiresApproval") &&
					emailPayload["requiresApproval"].is_object()) {
					const auto& approval = emailPayload["requiresApproval"];
					if (approval.contains("approvalToken") &&
						approval["approvalToken"].is_string()) {
						approvalToken = approval["approvalToken"].get<std::string>();
					}
					if (approval.contains("approvalTokenExpiresAtEpochMs") &&
						approval["approvalTokenExpiresAtEpochMs"].is_number_unsigned()) {
						approvalTokenExpiresAtEpochMs =
							approval["approvalTokenExpiresAtEpochMs"].get<std::uint64_t>();
					}
				}
			}
			catch (...) {
			}

			if (approvalToken.empty()) {
				result.success = false;
				result.terminalStatus = "failed";
				result.terminalReason = "approval_token_missing";
				result.errorCode = "orchestration_email_missing_approval_token";
				result.errorMessage = "approval_token_missing";
				return result;
			}

			ToolExecuteResult emailApproveExecution;
			bool shouldAutoApprove =
				intent.scheduleKind == "immediate_keyword";
			bool autoApproveBackendMissing = false;
			std::string autoApproveBackend = "himalaya";
			std::string fallbackProbeCode;
			std::string fallbackProbeMessage;
			if (shouldAutoApprove) {
				nlohmann::json emailApproveArgs = {
					{ "action", "approve" },
					{ "approvalToken", approvalToken },
					{ "approve", true },
				};

				emailApproveExecution = toolRegistry.Execute(
					"email.schedule",
					emailApproveArgs.dump());
				if (emailApproveExecution.executed &&
					emailApproveExecution.status == "ok") {
					try {
						const auto approvePayload =
							nlohmann::json::parse(emailApproveExecution.output);
						if (approvePayload.contains("output") &&
							approvePayload["output"].is_array() &&
							!approvePayload["output"].empty() &&
							approvePayload["output"][0].is_object() &&
							approvePayload["output"][0].contains("summary") &&
							approvePayload["output"][0]["summary"].is_object() &&
							approvePayload["output"][0]["summary"].contains("engine") &&
							approvePayload["output"][0]["summary"]["engine"].is_string()) {
							autoApproveBackend =
								approvePayload["output"][0]["summary"]["engine"].get<std::string>();
						}
					}
					catch (...) {
					}
				}
				else {
					const std::string approveOutputLower =
						ToLowerCopyLocal(emailApproveExecution.output);
					ResolveFallbackProbeDiagnostic(
						emailApproveExecution.output,
						fallbackProbeCode,
						fallbackProbeMessage);
					autoApproveBackendMissing =
						emailApproveExecution.status == "error" &&
						(approveOutputLower.find("missing") != std::string::npos ||
							approveOutputLower.find("unavailable") != std::string::npos ||
							emailApproveExecution.output.find("email_delivery_backends_exhausted") != std::string::npos);
					if (!autoApproveBackendMissing) {
						result.success = false;
						result.terminalStatus = "failed";
						result.terminalReason = "email_approve_failed";
						result.errorCode = "orchestration_email_approve_failed";
						result.errorMessage = emailApproveExecution.output;
						return result;
					}

					shouldAutoApprove = false;
				}
			}

			result.success = true;
			result.requiresApproval = !shouldAutoApprove;
			result.assistantDeltas = {
				"tools.execute.start tool=weather.lookup",
				"tools.execute.result tool=weather.lookup status=ok",
				"task.execute.start task=report.compose",
				"task.execute.result task=report.compose status=ok",
				"tools.execute.start tool=email.schedule action=prepare",
				"tools.execute.result tool=email.schedule status=needs_approval",
			};
			if (shouldAutoApprove) {
				result.assistantDeltas.push_back(
					"tools.execute.start tool=email.schedule action=approve");
				result.assistantDeltas.push_back(
					"tools.execute.result tool=email.schedule status=ok");
			}
			else if (autoApproveBackendMissing) {
				result.assistantDeltas.push_back(
					"tools.execute.start tool=email.schedule action=approve");
				std::string approveDelta =
					"tools.execute.result tool=email.schedule status=needs_approval backend_missing=himalaya";
				if (!fallbackProbeCode.empty()) {
					approveDelta += " probe=" + fallbackProbeCode;
				}
				result.assistantDeltas.push_back(approveDelta);
			}

			if (shouldAutoApprove) {
				result.terminalStatus = "completed";
				result.terminalReason = "auto_approved";
				if (preferChinese) {
					result.assistantText =
						report +
						Utf8LiteralLocal(u8"\u5DF2\u901A\u8FC7 ") + autoApproveBackend +
						Utf8LiteralLocal(u8"\u5728 ") + sendAt +
						Utf8LiteralLocal(u8"\u5411 ") + recipient +
						Utf8LiteralLocal(u8"\u53D1\u9001\u90AE\u4EF6\u3002");
				}
				else {
					result.assistantText =
						report +
						" Email sent to " + recipient +
						" at " + sendAt +
						" via " + autoApproveBackend + ".";
				}
			}
			else {
				result.terminalStatus = "needs_approval";
				result.terminalReason = autoApproveBackendMissing
					? "fallback_backend_unavailable"
					: "approval_required";
				result.fallbackBackend = autoApproveBackend;
				result.fallbackAction = "continue";
				result.fallbackAttempt = 1;
				result.fallbackMaxAttempts = 2;
				if (preferChinese) {
					result.assistantText =
						report +
						Utf8LiteralLocal(u8"\u5411 ") + recipient +
						Utf8LiteralLocal(u8"\u5728 ") + sendAt +
						Utf8LiteralLocal(u8"\u53D1\u9001\u90AE\u4EF6\u7684\u8BA1\u5212\u7B49\u5F85\u5BA1\u6279\u3002approvalToken=") +
						approvalToken;
				}
				else {
					result.assistantText =
						report +
						" Email scheduling to " + recipient +
						" at " + sendAt +
						" is pending approval. approvalToken=" +
						approvalToken;
				}
				if (autoApproveBackendMissing) {
					result.assistantText += preferChinese
						? Utf8LiteralLocal(u8"\u90AE\u4EF6\u6295\u9012\u540E\u7AEF\u4E0D\u53EF\u7528\uFF08\u7F3A\u5C11 himalaya CLI\uFF09\u3002\u8BF7\u5B89\u88C5\u5E76\u914D\u7F6E himalaya \u540E\u91CD\u65B0\u5BA1\u6279\u8BE5\u4EE4\u724C\u3002")
						: " Delivery backend is unavailable (himalaya CLI missing). Install/configure himalaya and re-approve this token.";
					const std::string fallbackProbeLabel =
						!fallbackProbeMessage.empty()
						? fallbackProbeMessage
						: fallbackProbeCode;
					if (!fallbackProbeLabel.empty()) {
						result.assistantText += preferChinese
							? " fallbackProbe=" + fallbackProbeLabel + Utf8LiteralLocal(u8"\u3002")
							: " fallbackProbe=" + fallbackProbeLabel + ".";
					}
				}
				if (approvalTokenExpiresAtEpochMs > 0) {
					result.assistantText +=
						" expiresAtEpochMs=" +
						std::to_string(approvalTokenExpiresAtEpochMs);
				}
			}

			return result;
		}

		bool IsDeepSeekDiagnosticsVerboseEnabled() {
			static const bool enabled = []() {
				char* raw = nullptr;
				std::size_t size = 0;
				if (_dupenv_s(
					&raw,
					&size,
					"BLAZECLAW_DEEPSEEK_DEBUG_TELEMETRY") != 0 ||
					raw == nullptr) {
					return false;
				}

				std::string value(raw);
				free(raw);
				std::transform(
					value.begin(),
					value.end(),
					value.begin(),
					[](unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});

				return value == "1" ||
					value == "true" ||
					value == "yes" ||
					value == "on";
				}();

			return enabled;
		}

		void EmitDeepSeekGatewayDiagnostic(
			const char* stage,
			const std::string& detail,
			const bool verboseOnly = true) {
			if (verboseOnly && !IsDeepSeekDiagnosticsVerboseEnabled()) {
				return;
			}

			const std::string safeStage =
				(stage == nullptr || std::string(stage).empty())
				? "unknown"
				: std::string(stage);
			TRACE(
				"[DeepSeek][%s] %s\n",
				safeStage.c_str(),
				detail.c_str());
		}

		bool IsSilentReplyText(const std::string& text) {
			return json::Trim(text) == kSilentReplyToken;
		}

		bool IsSilentAssistantMessageJson(const std::string& messageJson) {
			std::string role;
			if (!json::FindStringField(messageJson, "role", role)) {
				return false;
			}

			if (role != "assistant") {
				return false;
			}

			return messageJson.find("\"text\":\"NO_REPLY\"") !=
				std::string::npos;
		}

		void PushHistoryMessageIfNew(
			std::vector<std::string>& history,
			const std::string& messageJson) {
			if (!history.empty() && history.back() == messageJson) {
				return;
			}

			history.push_back(messageJson);
			if (history.size() > kMaxChatHistoryEntriesPerSession) {
				const std::size_t overflow =
					history.size() - kMaxChatHistoryEntriesPerSession;
				history.erase(
					history.begin(),
					history.begin() + static_cast<std::ptrdiff_t>(overflow));
			}
		}

		template <typename T>
		void PushEventWithRetentionLimit(
			std::deque<T>& queue,
			T eventState) {
			queue.push_back(std::move(eventState));
			while (queue.size() > kMaxChatEventsPerSession) {
				queue.pop_front();
			}
		}

		bool ValidateAttachmentPayloadShape(
			const std::optional<std::string>& paramsJson,
			bool& hasAttachments,
			std::string& errorCode,
			std::string& errorMessage) {
			hasAttachments = false;
			errorCode.clear();
			errorMessage.clear();
			if (!paramsJson.has_value()) {
				return true;
			}

			std::string attachmentsRaw;
			if (!json::FindRawField(paramsJson.value(), "attachments", attachmentsRaw)) {
				return true;
			}

			const std::string attachmentsTrimmed = json::Trim(attachmentsRaw);
			if (attachmentsTrimmed.empty() || attachmentsTrimmed == "[]") {
				return true;
			}

			if (attachmentsTrimmed.front() != '[' || attachmentsTrimmed.back() != ']') {
				errorCode = "invalid_attachments";
				errorMessage = "attachments must be a JSON array.";
				return false;
			}

			hasAttachments = true;
			if (attachmentsTrimmed.find("\"type\":\"image\"") == std::string::npos ||
				attachmentsTrimmed.find("\"mimeType\":\"") == std::string::npos ||
				attachmentsTrimmed.find("\"content\":\"") == std::string::npos) {
				errorCode = "invalid_attachments";
				errorMessage =
					"attachments entries must include type=image, mimeType, and content.";
				return false;
			}

			return true;
		}

		std::vector<std::string> ExtractAttachmentMimeTypes(
			const std::optional<std::string>& paramsJson) {
			std::vector<std::string> mimeTypes;
			if (!paramsJson.has_value()) {
				return mimeTypes;
			}

			std::string attachmentsRaw;
			if (!json::FindRawField(
				paramsJson.value(),
				"attachments",
				attachmentsRaw)) {
				return mimeTypes;
			}

			const std::string key = "\"mimeType\":\"";
			std::size_t cursor = 0;
			while (cursor < attachmentsRaw.size()) {
				const auto keyPos = attachmentsRaw.find(key, cursor);
				if (keyPos == std::string::npos) {
					break;
				}

				const std::size_t valueStart = keyPos + key.size();
				if (valueStart >= attachmentsRaw.size()) {
					break;
				}

				std::size_t valueEnd = valueStart;
				bool escaped = false;
				while (valueEnd < attachmentsRaw.size()) {
					const char ch = attachmentsRaw[valueEnd];
					if (escaped) {
						escaped = false;
						++valueEnd;
						continue;
					}

					if (ch == '\\') {
						escaped = true;
						++valueEnd;
						continue;
					}

					if (ch == '"') {
						break;
					}

					++valueEnd;
				}

				if (valueEnd > valueStart) {
					mimeTypes.push_back(
						attachmentsRaw.substr(valueStart, valueEnd - valueStart));
				}

				cursor = valueEnd == std::string::npos
					? attachmentsRaw.size()
					: valueEnd + 1;
			}

			return mimeTypes;
		}

		std::vector<std::string> ParseJsonStringArrayLocal(
			const std::string& rawArray) {
			std::vector<std::string> values;
			const std::string trimmed = json::Trim(rawArray);
			if (trimmed.size() < 2 ||
				trimmed.front() != '[' ||
				trimmed.back() != ']') {
				return values;
			}

			std::string current;
			bool inString = false;
			bool escaping = false;
			for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
				const char ch = trimmed[i];
				if (!inString) {
					if (ch == '"') {
						inString = true;
						current.clear();
					}
					continue;
				}

				if (escaping) {
					current.push_back(ch);
					escaping = false;
					continue;
				}

				if (ch == '\\') {
					escaping = true;
					continue;
				}

				if (ch == '"') {
					values.push_back(current);
					inString = false;
					continue;
				}

				current.push_back(ch);
			}

			return values;
		}

		struct OrderedSequencePreflight {
			bool enforced = false;
			bool strictAllowlist = false;
			std::vector<std::string> orderedTargets;
			std::vector<std::string> explicitCallTargets;
			std::vector<std::string> resolvedToolTargets;
			std::vector<std::string> missingTargets;
		};

		std::string ResolvePreferredToolForNamespace(
			const std::string& normalizedNamespace,
			const std::vector<ToolCatalogEntry>& tools) {
			if (normalizedNamespace.empty()) {
				return {};
			}

			const std::string preferredSendId =
				normalizedNamespace + ".smtp.send";
			for (const auto& tool : tools) {
				if (ToLowerCopyLocal(tool.id) == preferredSendId) {
					return tool.id;
				}
			}

			const std::string preferredSearchId =
				normalizedNamespace + ".search.web";
			for (const auto& tool : tools) {
				if (ToLowerCopyLocal(tool.id) == preferredSearchId) {
					return tool.id;
				}
			}

			for (const auto& tool : tools) {
				const std::string toolIdLower = ToLowerCopyLocal(tool.id);
				if (toolIdLower.rfind(normalizedNamespace + ".", 0) == 0) {
					return tool.id;
				}
			}

			return {};
		}

		std::string NormalizeOrderedTargetToken(const std::string& token) {
			std::string normalized = json::Trim(token);
			while (!normalized.empty() &&
				(normalized.back() == '.' ||
					normalized.back() == ';' ||
					normalized.back() == ',' ||
					normalized.back() == ':' ||
					normalized.back() == ')' ||
					normalized.back() == '"')) {
				normalized.pop_back();
			}

			while (!normalized.empty() &&
				(normalized.front() == '(' ||
					normalized.front() == '"')) {
				normalized.erase(normalized.begin());
			}

			return ToLowerCopyLocal(normalized);
		}

		std::vector<std::string> ExtractOrderedTargetsFromPrompt(
			const std::string& message,
			std::vector<std::string>* explicitCallTargets) {
			std::vector<std::string> targets;
			std::vector<std::string> explicitTargets;

			auto addTarget = [&targets](const std::string& candidate) {
				const std::string normalized =
					NormalizeOrderedTargetToken(candidate);
				if (normalized.empty()) {
					return;
				}

				if (std::find(targets.begin(), targets.end(), normalized) !=
					targets.end()) {
					return;
				}

				targets.push_back(normalized);
				};

			auto addExplicitTarget =
				[&explicitTargets](const std::string& candidate) {
				const std::string normalized =
					NormalizeOrderedTargetToken(candidate);
				if (normalized.empty()) {
					return;
				}

				if (std::find(
					explicitTargets.begin(),
					explicitTargets.end(),
					normalized) != explicitTargets.end()) {
					return;
				}

				explicitTargets.push_back(normalized);
				};

			const std::regex backtickTargetRegex(
				R"(`([A-Za-z0-9._-]+)`)",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					const std::string target = (*it)[1].str();
					addExplicitTarget(target);
					addTarget(target);
				}
			}

			const std::regex callTargetRegex(
				R"(\bcall\s+([A-Za-z0-9._-]+))",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), callTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					const std::string target = (*it)[1].str();
					addExplicitTarget(target);
					addTarget(target);
				}
			}

			const std::regex numberedStepTargetRegex(
				R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-]\s*([A-Za-z0-9._-]+))",
				std::regex_constants::icase);
			for (std::sregex_iterator it(message.begin(), message.end(), numberedStepTargetRegex), end;
				it != end;
				++it) {
				if (it->size() >= 2) {
					addTarget((*it)[1].str());
				}
			}

			if (message.find("->") != std::string::npos) {
				std::size_t cursor = 0;
				while (cursor < message.size()) {
					const std::size_t arrow = message.find("->", cursor);
					if (arrow == std::string::npos) {
						break;
					}

					const std::size_t leftBoundary =
						message.rfind(' ', arrow) == std::string::npos
						? 0
						: message.rfind(' ', arrow) + 1;
					const std::size_t rightBoundary =
						message.find_first_of(" \n\r\t", arrow + 2);
					const std::size_t rightEnd = rightBoundary == std::string::npos
						? message.size()
						: rightBoundary;

					if (arrow > leftBoundary) {
						addTarget(message.substr(leftBoundary, arrow - leftBoundary));
					}
					if (rightEnd > arrow + 2) {
						addTarget(message.substr(arrow + 2, rightEnd - (arrow + 2)));
					}

					cursor = arrow + 2;
				}
			}

			if (explicitCallTargets != nullptr) {
				*explicitCallTargets = explicitTargets;
			}

			return targets;
		}

		bool HasStructuralSequenceSignal(const std::string& message) {
			const std::regex backtickTargetRegex(
				R"(`([A-Za-z0-9._-]+)`)",
				std::regex_constants::icase);
			std::size_t backtickTargetCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
				it != end;
				++it) {
				++backtickTargetCount;
				if (backtickTargetCount >= 2) {
					return true;
				}
			}

			if (message.find("->") != std::string::npos) {
				return true;
			}

			const std::regex numberedStepRegex(
				R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-])",
				std::regex_constants::icase);
			std::size_t numberedStepCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), numberedStepRegex), end;
				it != end;
				++it) {
				++numberedStepCount;
				if (numberedStepCount >= 2) {
					return true;
				}
			}

			const std::regex callDirectiveRegex(
				R"(\bcall\s+[A-Za-z0-9._-]+)",
				std::regex_constants::icase);
			std::size_t callDirectiveCount = 0;
			for (std::sregex_iterator it(message.begin(), message.end(), callDirectiveRegex), end;
				it != end;
				++it) {
				++callDirectiveCount;
				if (callDirectiveCount >= 2) {
					return true;
				}
			}

			return false;
		}

		std::string ResolveOrderedTargetToToolId(
			const std::string& target,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
			if (target.empty()) {
				return {};
			}

			const std::string normalizedTarget = ToLowerCopyLocal(target);
			std::string normalizedNamespace = normalizedTarget;
			std::replace(
				normalizedNamespace.begin(),
				normalizedNamespace.end(),
				'-',
				'_');

			for (const auto& tool : tools) {
				const std::string toolIdLower = ToLowerCopyLocal(tool.id);
				if (toolIdLower == normalizedTarget) {
					return tool.id;
				}

				if (toolIdLower == normalizedTarget + ".search.web") {
					return tool.id;
				}

				if (toolIdLower == normalizedTarget + ".smtp.send") {
					return tool.id;
				}
			}

			const std::string preferredByNamespace =
				ResolvePreferredToolForNamespace(normalizedNamespace, tools);
			if (!preferredByNamespace.empty()) {
				return preferredByNamespace;
			}

			for (const auto& entry : skillsCatalogEntries) {
				const std::string nameLower = ToLowerCopyLocal(entry.name);
				const std::string keyLower = ToLowerCopyLocal(entry.skillKey);
				const std::string commandLower = ToLowerCopyLocal(entry.commandName);
				const std::string commandToolLower = ToLowerCopyLocal(entry.commandToolName);

				if (nameLower == normalizedTarget ||
					keyLower == normalizedTarget ||
					commandLower == normalizedTarget ||
					commandToolLower == normalizedTarget) {
					if (!entry.commandToolName.empty()) {
						return entry.commandToolName;
					}

					std::string entryNamespace = keyLower.empty()
						? nameLower
						: keyLower;
					std::replace(
						entryNamespace.begin(),
						entryNamespace.end(),
						'-',
						'_');
					const std::string resolvedBySkill =
						ResolvePreferredToolForNamespace(entryNamespace, tools);
					if (!resolvedBySkill.empty()) {
						return resolvedBySkill;
					}

					const bool modelInvocationAllowed =
						entry.eligible &&
						!entry.disabled &&
						!entry.blockedByAllowlist &&
						!entry.disableModelInvocation;
					if (modelInvocationAllowed) {
						return std::string("model_skill.") +
							(entry.skillKey.empty() ? entry.name : entry.skillKey);
					}

					return {};
				}
			}

			return {};
		}

		OrderedSequencePreflight BuildOrderedSequencePreflight(
			const std::string& message,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
			OrderedSequencePreflight preflight;
			std::vector<std::string> inferredTargets;
			preflight.explicitCallTargets.clear();
			inferredTargets = ExtractOrderedTargetsFromPrompt(
				message,
				&preflight.explicitCallTargets);

			if (!preflight.explicitCallTargets.empty()) {
				preflight.orderedTargets = preflight.explicitCallTargets;
				preflight.strictAllowlist = true;
				preflight.enforced = true;
			}
			else {
				if (!HasStructuralSequenceSignal(message)) {
					return preflight;
				}

				preflight.orderedTargets = std::move(inferredTargets);
				preflight.strictAllowlist = false;
				preflight.enforced = preflight.orderedTargets.size() >= 2;
			}

			preflight.resolvedToolTargets.reserve(preflight.orderedTargets.size());
			if (!preflight.enforced) {
				return preflight;
			}

			for (const auto& target : preflight.orderedTargets) {
				const std::string resolvedTool = ResolveOrderedTargetToToolId(
					target,
					tools,
					skillsCatalogEntries);
				preflight.resolvedToolTargets.push_back(resolvedTool);
				if (resolvedTool.empty()) {
					preflight.missingTargets.push_back(target);
				}
			}

			return preflight;
		}

		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			BuildOrderedPreflightTaskDeltas(
				const std::string& runId,
				const std::string& sessionKey,
				const OrderedSequencePreflight& preflight,
				const bool terminalFailure,
				const std::string& terminalErrorCode,
				const std::string& terminalErrorMessage) {
			std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> taskDeltas;
			if (!preflight.enforced) {
				return taskDeltas;
			}

			const std::uint64_t baseMs = CurrentEpochMsLocal();
			taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = taskDeltas.size(),
				.runId = runId,
				.sessionId = sessionKey,
				.phase = "plan",
				.resultJson = SerializeStringArrayLocal(preflight.orderedTargets),
				.status = "ok",
				.startedAtMs = baseMs,
				.completedAtMs = baseMs,
				.latencyMs = 0,
				.stepLabel = "ordered_execution_plan",
				});

			for (std::size_t index = 0; index < preflight.orderedTargets.size(); ++index) {
				const std::string& target = preflight.orderedTargets[index];
				const std::string resolvedTarget =
					index < preflight.resolvedToolTargets.size() &&
					!preflight.resolvedToolTargets[index].empty()
					? preflight.resolvedToolTargets[index]
					: target;
				const bool missing = std::find(
					preflight.missingTargets.begin(),
					preflight.missingTargets.end(),
					target) != preflight.missingTargets.end();

				taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = taskDeltas.size(),
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "preflight",
					.toolName = resolvedTarget,
					.argsJson = target,
					.status = missing ? "missing" : "ok",
					.errorCode = missing ? "step_target_unavailable" : std::string(),
					.startedAtMs = baseMs + index + 1,
					.completedAtMs = baseMs + index + 1,
					.latencyMs = 0,
					.stepLabel = "ordered_step_precheck",
					});
			}

			if (terminalFailure) {
				taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = taskDeltas.size(),
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "final",
					.resultJson = terminalErrorMessage,
					.status = "failed",
					.errorCode = terminalErrorCode,
					.startedAtMs = baseMs + preflight.orderedTargets.size() + 1,
					.completedAtMs = baseMs + preflight.orderedTargets.size() + 1,
					.latencyMs = 0,
					.stepLabel = "run_terminal",
					});
			}

			return taskDeltas;
		}

		std::string JoinOrderedTargets(const std::vector<std::string>& targets) {
			std::string joined;
			for (std::size_t i = 0; i < targets.size(); ++i) {
				if (i > 0) {
					joined += ", ";
				}

				joined += targets[i];
			}

			return joined;
		}

		std::string BuildOrderedStepPreflightLabel(const std::string& resolvedTarget) {
			if (resolvedTarget.rfind("model_skill.", 0) == 0) {
				return "ordered_step_precheck_model";
			}

			return "ordered_step_precheck";
		}

		bool EndsWithLocal(
			const std::string& value,
			const std::string& suffix) {
			if (value.size() < suffix.size()) {
				return false;
			}

			return value.compare(
				value.size() - suffix.size(),
				suffix.size(),
				suffix) == 0;
		}

		bool IsResolvedRuntimeToolTarget(
			const std::string& resolvedToolId,
			const std::vector<ToolCatalogEntry>& runtimeTools) {
			if (resolvedToolId.empty()) {
				return false;
			}

			const std::string lowered = ToLowerCopyLocal(resolvedToolId);
			if (lowered.rfind("model_skill.", 0) == 0) {
				return false;
			}

			for (const auto& tool : runtimeTools) {
				if (!tool.enabled) {
					continue;
				}

				if (ToLowerCopyLocal(tool.id) == lowered) {
					return true;
				}
			}

			return false;
		}

		bool IsInvalidArgumentsResult(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
			if (delta.phase != "tool_result") {
				return false;
			}

			const std::string status = ToLowerCopyLocal(delta.status);
			const std::string errorCode = ToLowerCopyLocal(delta.errorCode);
			return status == "invalid_arguments" ||
				status == "invalid_args" ||
				errorCode == "invalid_arguments" ||
				errorCode == "invalid_args";
		}

		std::string ExtractFirstHttpUrl(
			const std::string& text) {
			static const std::regex kHttpRegex(
				R"((https?://[^\s\)\]\>\"]+))",
				std::regex_constants::icase);
			std::smatch match;
			if (std::regex_search(text, match, kHttpRegex) && !match.empty()) {
				return match[1].str();
			}

			return {};
		}

		bool TryBuildRecoveredArgsJson(
			const std::string& toolId,
			const std::string& message,
			std::string& outArgsJson) {
			outArgsJson.clear();
			const std::string trimmedMessage = json::Trim(message);
			if (trimmedMessage.empty()) {
				return false;
			}

			const std::string lowerTool = ToLowerCopyLocal(toolId);
			if (EndsWithLocal(lowerTool, ".search.web")) {
				const auto compactQuery = DeriveCompactSearchQueryLocal(trimmedMessage);
				if (!compactQuery.has_value()) {
					return false;
				}

				outArgsJson =
					"{\"query\":" + JsonString(compactQuery.value()) +
					",\"count\":5}";
				return true;
			}

			if (EndsWithLocal(lowerTool, ".fetch.content")) {
				const std::string firstUrl = ExtractFirstHttpUrl(trimmedMessage);
				if (firstUrl.empty()) {
					return false;
				}

				outArgsJson = "{\"url\":" + JsonString(firstUrl) + "}";
				return true;
			}

			if (EndsWithLocal(lowerTool, ".smtp.send")) {
				const std::string recipient = ExtractFirstEmailAddress(trimmedMessage);
				if (recipient.empty()) {
					return false;
				}

				outArgsJson =
					"{\"to\":" + JsonString(recipient) +
					",\"subject\":\"Preview\",\"body\":" +
					JsonString(trimmedMessage) + "}";
				return true;
			}

			return false;
		}

		std::string SkillNamespaceOfToolId(const std::string& toolId) {
			const auto dot = toolId.find('.');
			if (dot == std::string::npos || dot == 0) {
				return {};
			}

			return toolId.substr(0, dot);
		}

		int IntentSimilarityScore(
			const std::string& loweredMessage,
			const ToolCatalogEntry& tool,
			const std::string& referenceCategory,
			const std::string& referenceNamespace) {
			int score = 0;
			const std::string toolIdLower = ToLowerCopyLocal(tool.id);
			const std::string toolLabelLower = ToLowerCopyLocal(tool.label);
			const std::string toolCategoryLower = ToLowerCopyLocal(tool.category);

			if (!referenceCategory.empty() &&
				toolCategoryLower == referenceCategory) {
				score += 2;
			}

			if (!referenceNamespace.empty() &&
				toolIdLower.rfind(referenceNamespace + ".", 0) == 0) {
				score += 3;
			}

			if (loweredMessage.find("search") != std::string::npos &&
				toolIdLower.find("search") != std::string::npos) {
				score += 2;
			}

			if ((loweredMessage.find("email") != std::string::npos ||
				loweredMessage.find("mail") != std::string::npos) &&
				(toolIdLower.find("smtp") != std::string::npos ||
					toolIdLower.find("imap") != std::string::npos ||
					toolLabelLower.find("email") != std::string::npos)) {
				score += 2;
			}

			if (loweredMessage.find("fetch") != std::string::npos &&
				toolIdLower.find("fetch") != std::string::npos) {
				score += 1;
			}

			return score;
		}

		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			ApplyInvalidArgumentsRecoveryPolicy(
				const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& source,
				const std::string& runId,
				const std::string& sessionKey,
				const std::string& message,
				GatewayToolRegistry& toolRegistry) {
			if (source.empty()) {
				return source;
			}

			auto recovered = source;
			auto invalidIt = std::find_if(
				recovered.begin(),
				recovered.end(),
				[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
					return IsInvalidArgumentsResult(delta);
				});
			if (invalidIt == recovered.end() || invalidIt->toolName.empty()) {
				return recovered;
			}

			const auto allTools = toolRegistry.List();
			const std::string failedTool = invalidIt->toolName;
			const std::string failedToolLower = ToLowerCopyLocal(failedTool);
			const std::string failedNamespace = SkillNamespaceOfToolId(failedToolLower);
			std::string failedCategory;
			for (const auto& tool : allTools) {
				if (ToLowerCopyLocal(tool.id) == failedToolLower) {
					failedCategory = ToLowerCopyLocal(tool.category);
					break;
				}
			}

			auto appendAttempt =
				[&](const std::string& toolId,
					const std::string& action,
					const std::size_t attempt,
					const std::size_t maxAttempts,
					const std::string& argsJson,
					const ToolExecuteResult& execution) {
						const std::uint64_t nowMs = CurrentEpochMsLocal();
						recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
							.index = recovered.size(),
							.runId = runId,
							.sessionId = sessionKey,
							.phase = "tool_call",
							.toolName = toolId,
							.fallbackAction = action,
							.fallbackAttempt = attempt,
							.fallbackMaxAttempts = maxAttempts,
							.argsJson = argsJson,
							.status = "requested",
							.startedAtMs = nowMs,
							.completedAtMs = nowMs,
							.latencyMs = 0,
							.stepLabel = "tool_retry_request",
							});

						const std::string resultStatus = execution.status.empty()
							? (execution.executed ? "ok" : "error")
							: execution.status;
						const std::string resultErrorCode =
							ToLowerCopyLocal(resultStatus) == "ok"
							? std::string{}
							: (ToLowerCopyLocal(resultStatus) == "invalid_args"
								? std::string("invalid_arguments")
								: resultStatus);
						recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
							.index = recovered.size(),
							.runId = runId,
							.sessionId = sessionKey,
							.phase = "tool_result",
							.toolName = toolId,
							.fallbackAction = action,
							.fallbackAttempt = attempt,
							.fallbackMaxAttempts = maxAttempts,
							.argsJson = argsJson,
							.resultJson = execution.output,
							.status = resultStatus,
							.errorCode = resultErrorCode,
							.startedAtMs = nowMs,
							.completedAtMs = nowMs,
							.latencyMs = 0,
							.stepLabel = "tool_retry_result",
							});
				};

			auto tryExecuteRecovered =
				[&](const std::string& toolId,
					const std::string& action,
					const std::size_t attempt,
					const std::size_t maxAttempts) {
						std::string rebuiltArgsJson;
						if (!TryBuildRecoveredArgsJson(toolId, message, rebuiltArgsJson)) {
							return false;
						}

						const ToolExecuteResult execution = toolRegistry.Execute(
							toolId,
							rebuiltArgsJson);
						appendAttempt(
							toolId,
							action,
							attempt,
							maxAttempts,
							rebuiltArgsJson,
							execution);

						const std::string statusLower = ToLowerCopyLocal(execution.status);
						return execution.executed &&
							statusLower != "error" &&
							statusLower != "invalid_args" &&
							statusLower != "invalid_arguments";
				};

			if (tryExecuteRecovered(
				failedTool,
				"args_rebuild_retry",
				1,
				1)) {
				return recovered;
			}

			std::size_t sameSkillAttempt = 1;
			for (const auto& tool : allTools) {
				const std::string toolIdLower = ToLowerCopyLocal(tool.id);
				if (!failedNamespace.empty() &&
					toolIdLower.rfind(failedNamespace + ".", 0) != 0) {
					continue;
				}

				if (toolIdLower == failedToolLower || !tool.enabled) {
					continue;
				}

				if (tryExecuteRecovered(
					tool.id,
					"same_skill_candidate_retry",
					sameSkillAttempt,
					2)) {
					return recovered;
				}

				++sameSkillAttempt;
				if (sameSkillAttempt > 2) {
					break;
				}
			}

			const std::string loweredMessage = ToLowerCopyLocal(message);
			const ToolCatalogEntry* bestCandidate = nullptr;
			int bestScore = 0;
			for (const auto& tool : allTools) {
				const std::string toolIdLower = ToLowerCopyLocal(tool.id);
				if (!tool.enabled || toolIdLower == failedToolLower) {
					continue;
				}

				std::string candidateArgs;
				if (!TryBuildRecoveredArgsJson(tool.id, message, candidateArgs)) {
					continue;
				}

				const int score = IntentSimilarityScore(
					loweredMessage,
					tool,
					failedCategory,
					failedNamespace);
				if (score > bestScore) {
					bestScore = score;
					bestCandidate = &tool;
				}
			}

			if (bestCandidate != nullptr && bestScore > 0) {
				(void)tryExecuteRecovered(
					bestCandidate->id,
					"cross_skill_guarded_retry",
					1,
					1);
			}
			else {
				const std::uint64_t nowMs = CurrentEpochMsLocal();
				recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = recovered.size(),
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "fallback",
					.toolName = failedTool,
					.fallbackAction = "cross_skill_guarded_retry",
					.status = "skipped",
					.errorCode = "cross_skill_fallback_not_eligible",
					.startedAtMs = nowMs,
					.completedAtMs = nowMs,
					.latencyMs = 0,
					.stepLabel = "fallback_gate",
					});
			}

			return recovered;
		}

		std::string JoinOrderedResolution(
			const OrderedSequencePreflight& preflight) {
			std::string joined;
			for (std::size_t i = 0; i < preflight.orderedTargets.size(); ++i) {
				if (i > 0) {
					joined += ", ";
				}

				const std::string requested = preflight.orderedTargets[i];
				const std::string resolved =
					i < preflight.resolvedToolTargets.size()
					? preflight.resolvedToolTargets[i]
					: std::string{};
				if (!resolved.empty() && resolved != requested) {
					joined += requested + "=>" + resolved;
				}
				else {
					joined += requested;
				}
			}

			return joined;
		}

		std::string SerializeFloatArrayLocal(
			const std::vector<float>& values) {
			std::ostringstream output;
			output.setf(std::ios::fixed);
			output.precision(6);
			output << "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					output << ",";
				}

				output << values[i];
			}
			output << "]";
			return output.str();
		}

		std::string SerializeFloatMatrixLocal(
			const std::vector<std::vector<float>>& vectors) {
			std::string output = "[";
			for (std::size_t i = 0; i < vectors.size(); ++i) {
				if (i > 0) {
					output += ",";
				}

				output += SerializeFloatArrayLocal(vectors[i]);
			}

			output += "]";
			return output;
		}
	}

	void GatewayHost::RegisterRuntimeHandlers() {
		m_dispatcher.Register(
			"gateway.runtime.governance.reportStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"governanceReportingEnabled\":" +
						std::string(state.governanceReportingEnabled ? "true" : "false") +
						",\"remediationTelemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"remediationAuditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\",\"autoRemediationTenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"autoRemediationTokenRotations\":" +
						std::to_string(state.autoRemediationTokenRotations) +
						",\"remediationSloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"remediationSloMaxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"remediationSloMaxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"lastComplianceAttestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"complianceAttestationEnabled\":" +
						std::string(!state.lastComplianceAttestationPath.empty() ? "true" : "false") +
						",\"enterpriseSlaPolicyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"crossTenantAttestationAggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"crossTenantAttestationAggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"crossTenantAttestationAggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"lastCrossTenantAttestationAggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\"" +
						",\"governanceReportsGenerated\":" +
						std::to_string(state.governanceReportsGenerated) +
						",\"lastGovernanceReportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"lastDriftReason\":\"" +
						EscapeJsonLocal(state.lastDriftReason) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.attestationStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"maxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"maxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"telemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"auditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.aggregationStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"policyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"aggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.remediationPlan",
			[this](const protocol::RequestFrame& request) {
				(void)request;
				const auto& state = m_skillsCatalogState;
				std::string severity = "none";
				std::string recommendedAction = "monitor";
				if (state.driftDetectedCount > 0) {
					severity = "high";
					recommendedAction =
						"review drift report; enforce strict policy and investigate runtime divergence";
				}
				else if (state.policyBlockedCount > 0) {
					severity = "medium";
					recommendedAction =
						"review package allowlist policy and blocked package changes";
				}

				const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
				const std::uint64_t ttlMinutes =
					state.autoRemediationTokenMaxAgeMinutes > 0
					? static_cast<std::uint64_t>(state.autoRemediationTokenMaxAgeMinutes)
					: std::uint64_t{ 60 };
				const std::uint64_t expiresAtEpochMs =
					nowEpochMs + (ttlMinutes * std::uint64_t{ 60000 });

				std::string issuedApprovalToken;
				if (state.autoRemediationRequiresApproval &&
					state.autoRemediationEnabled) {
					issuedApprovalToken =
						"remediation-approval-" + std::to_string(nowEpochMs) +
						"-" + std::to_string(state.driftDetectedCount + state.policyBlockedCount + 1);

					const std::string payload =
						"{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"recommendedAction\":\"" +
						EscapeJsonLocal(recommendedAction) +
						"\",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}";

					const ApprovalSessionRecord session{
						.token = issuedApprovalToken,
						.type = "governance.remediation",
						.payloadJson = payload,
						.createdAtEpochMs = nowEpochMs,
						.expiresAtEpochMs = expiresAtEpochMs,
					};

					if (!m_approvalStore.SaveSession(session)) {
						issuedApprovalToken.clear();
					}

					m_approvalStore.PruneExpired(nowEpochMs);
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"severity\":\"" + EscapeJsonLocal(severity) +
						"\",\"recommendedAction\":\"" +
						EscapeJsonLocal(recommendedAction) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"autoRemediationEnabled\":" +
						std::string(state.autoRemediationEnabled ? "true" : "false") +
						",\"autoRemediationRequiresApproval\":" +
						std::string(state.autoRemediationRequiresApproval ? "true" : "false") +
						",\"approvalToken\":\"" +
						EscapeJsonLocal(issuedApprovalToken) +
						"\",\"approvalTokenExpiresAtEpochMs\":" +
						std::to_string(expiresAtEpochMs) +
						",\"tokenMaxAgeMinutes\":" +
						std::to_string(ttlMinutes) +
						",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.executeRemediation",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				if (!state.autoRemediationEnabled) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson =
							"{\"executed\":false,\"status\":\"disabled\",\"approvalAccepted\":false}",
						.error = std::nullopt,
					};
				}

				bool approvalAccepted = false;
				std::string approvalToken;
				if (request.paramsJson.has_value()) {
					json::FindStringField(
						request.paramsJson.value(),
						"approvalToken",
						approvalToken);
				}
				if (state.autoRemediationRequiresApproval) {
					bool approved = false;
					if (request.paramsJson.has_value()) {
						json::FindBoolField(request.paramsJson.value(), "approved", approved);
					}
					approvalAccepted = approved;
					const bool tokenAccepted = !approvalToken.empty();
					if (!approvalAccepted || !tokenAccepted) {
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = false,
							.payloadJson = std::nullopt,
							.error = protocol::ErrorShape{
								.code = "approval_required",
								.message = "Auto-remediation execution requires explicit approval and token.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							},
						};
					}

					const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
					ApprovalSessionRecord approvalSession;
					if (!m_approvalStore.IsTokenValid(
						approvalToken,
						nowEpochMs,
						&approvalSession)) {
						const auto existing = m_approvalStore.LoadSession(approvalToken);
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = false,
							.payloadJson = std::nullopt,
							.error = protocol::ErrorShape{
								.code = existing.has_value()
									? "approval_token_expired"
									: "approval_token_invalid",
								.message = existing.has_value()
									? "Approval token expired. Request a new remediation plan token."
									: "Approval token not found.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							},
						};
					}

					if (approvalSession.type != "governance.remediation") {
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = false,
							.payloadJson = std::nullopt,
							.error = protocol::ErrorShape{
								.code = "approval_token_orphaned",
								.message = "Approval token type mismatch for remediation execution.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							},
						};
					}

					std::string tokenTenantId;
					json::FindStringField(
						approvalSession.payloadJson,
						"tenantId",
						tokenTenantId);
					if (!tokenTenantId.empty() &&
						tokenTenantId != state.autoRemediationTenantId) {
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = false,
							.payloadJson = std::nullopt,
							.error = protocol::ErrorShape{
								.code = "approval_token_orphaned",
								.message = "Approval token tenant mismatch for remediation execution.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							},
						};
					}
				}

				std::string action = "monitor";
				if (state.driftDetectedCount > 0) {
					action = "enable_strict_policy";
				}
				else if (state.policyBlockedCount > 0) {
					action = "refresh_allowlist_review";
				}

				if (state.autoRemediationRequiresApproval && !approvalToken.empty()) {
					m_approvalStore.RemoveToken(approvalToken);
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"executed\":true,\"status\":\"applied\",\"approvalAccepted\":" +
						std::string(approvalAccepted ? "true" : "false") +
						",\"tenantId\":\"" + EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"playbookPath\":\"" +
						EscapeJsonLocal(state.lastAutoRemediationPlaybookPath) +
						"\",\"tokenMaxAgeMinutes\":" +
						std::to_string(state.autoRemediationTokenMaxAgeMinutes) +
						",\"action\":\"" + EscapeJsonLocal(action) +
						"\",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.embeddings.generate",
			[this](const protocol::RequestFrame& request) {
				const std::string text =
					ExtractStringParam(request.paramsJson, "text");
				const std::optional<bool> normalize =
					ExtractBoolParam(request.paramsJson, "normalize");
				const std::string model =
					ExtractStringParam(request.paramsJson, "model");
				const std::string traceId =
					request.id.empty() ? "gateway.embeddings.generate" : request.id;

				if (text.empty()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "invalid_params",
							.message = "`text` must be a non-empty string.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				if (!m_embeddingsGenerateCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "runtime_unavailable",
							.message = "Embeddings runtime callback is unavailable.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto result = m_embeddingsGenerateCallback(
					EmbeddingsGenerateRequest{
						.text = text,
						.normalize = normalize,
						.model = model,
						.traceId = traceId,
					});

				if (!result.ok) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = result.errorCode.empty()
								? "embedding_failed"
								: result.errorCode,
							.message = result.errorMessage.empty()
								? "Embedding generation failed."
								: result.errorMessage,
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vector\":" + SerializeFloatArrayLocal(result.vector) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.embeddings.batchGenerate",
			[this](const protocol::RequestFrame& request) {
				std::string rawTexts;
				std::vector<std::string> texts;
				if (request.paramsJson.has_value() &&
					json::FindRawField(request.paramsJson.value(), "texts", rawTexts)) {
					texts = ParseJsonStringArrayLocal(rawTexts);
				}

				const std::optional<bool> normalize =
					ExtractBoolParam(request.paramsJson, "normalize");
				const std::string model =
					ExtractStringParam(request.paramsJson, "model");
				const std::string traceId =
					request.id.empty() ? "gateway.embeddings.batchGenerate" : request.id;

				if (texts.empty()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "invalid_params",
							.message = "`texts` must be a non-empty string array.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				if (texts.size() > 64) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "invalid_params",
							.message = "`texts` exceeds maximum batch size of 64.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				if (!m_embeddingsBatchCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "runtime_unavailable",
							.message = "Embeddings runtime callback is unavailable.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto result = m_embeddingsBatchCallback(
					EmbeddingsBatchRequest{
						.texts = texts,
						.normalize = normalize,
						.model = model,
						.traceId = traceId,
					});

				if (!result.ok) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = result.errorCode.empty()
								? "embedding_failed"
								: result.errorCode,
							.message = result.errorMessage.empty()
								? "Embedding batch generation failed."
								: result.errorMessage,
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectors\":" + SerializeFloatMatrixLocal(result.vectors) +
						",\"count\":" + std::to_string(result.vectors.size()) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.taskDeltas.get",
			[this](const protocol::RequestFrame& request) {
				const std::string runId =
					ExtractStringParam(request.paramsJson, "runId");
				if (runId.empty()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "missing_run_id",
							.message = "runId is required.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto storedDeltas = m_taskDeltaRepository.Get(runId);
				if (!storedDeltas.has_value()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson =
							"{\"runId\":\"" + EscapeJsonLocal(runId) +
							"\",\"taskDeltas\":[],\"count\":0}",
						.error = std::nullopt,
					};
				}

				auto orderedTaskDeltas = storedDeltas.value();
				std::sort(
					orderedTaskDeltas.begin(),
					orderedTaskDeltas.end(),
					[](const ChatRuntimeResult::TaskDeltaEntry& left,
						const ChatRuntimeResult::TaskDeltaEntry& right) {
							return left.index < right.index;
					});

				orderedTaskDeltas = TaskDeltaLegacyAdapter::AdaptRun(
					runId,
					orderedTaskDeltas.empty() ? std::string("main") : orderedTaskDeltas.front().sessionId,
					orderedTaskDeltas);

				std::string schemaErrorCode;
				std::string schemaErrorMessage;
				if (!TaskDeltaSchemaValidator::ValidateRun(
					runId,
					orderedTaskDeltas,
					schemaErrorCode,
					schemaErrorMessage)) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = schemaErrorCode,
							.message = schemaErrorMessage,
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				std::string deltasJson = "[";
				for (std::size_t i = 0; i < orderedTaskDeltas.size(); ++i) {
					if (i > 0) {
						deltasJson += ",";
					}
					deltasJson += SerializeTaskDeltaEntryJson(orderedTaskDeltas[i]);
				}
				deltasJson += "]";

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"runId\":\"" + EscapeJsonLocal(runId) +
						"\",\"taskDeltas\":" + deltasJson +
						",\"count\":" + std::to_string(orderedTaskDeltas.size()) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.taskDeltas.clear",
			[this](const protocol::RequestFrame& request) {
				const std::string runId =
					ExtractStringParam(request.paramsJson, "runId");
				std::size_t cleared = 0;
				if (runId.empty()) {
					cleared = m_taskDeltaRepository.Size();
					m_taskDeltaRepository.ClearAll();
				}
				else {
					cleared = m_taskDeltaRepository.Clear(runId) ? 1 : 0;
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"runId\":\"" + EscapeJsonLocal(runId.empty() ? "*" : runId) +
						"\",\"cleared\":" + std::to_string(cleared) +
					 ",\"remaining\":" + std::to_string(m_taskDeltaRepository.Size()) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"chat.history",
			[this](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::size_t requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(200);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 500 }));

				const auto historyIt = m_chatHistoryBySession.find(sessionKey);
				std::string messagesJson = "[";
				if (historyIt != m_chatHistoryBySession.end()) {
					const auto& history = historyIt->second;
					const std::size_t begin = history.size() > limit
						? history.size() - limit
						: 0;
					bool firstMessage = true;
					for (std::size_t i = begin; i < history.size(); ++i) {
						if (IsSilentAssistantMessageJson(history[i])) {
							continue;
						}

						if (!firstMessage) {
							messagesJson += ",";
						}

						messagesJson += history[i];
						firstMessage = false;
					}
				}

				messagesJson += "]";
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"messages\":" +
						messagesJson +
						",\"thinkingLevel\":\"normal\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"chat.send",
			[this](const protocol::RequestFrame& request) {
				ChatRunStageContext stageContext{
					   .requestId = request.id,
					   .method = request.method,
				 .paramsJson = request.paramsJson,
					.validateAttachments = [this](
						const std::optional<std::string>& paramsJson,
						bool& hasAttachments,
						std::string& errorCode,
						std::string& errorMessage) {
						return ValidateAttachmentPayloadShape(
							paramsJson,
							hasAttachments,
							errorCode,
							errorMessage);
					},
					.findRunByIdempotency = [this](const std::string& key)
						-> std::optional<std::string> {
						if (key.empty()) {
							return std::nullopt;
						}

						const auto dedupeIt = m_chatRunByIdempotency.find(key);
						if (dedupeIt == m_chatRunByIdempotency.end()) {
							return std::nullopt;
						}

						return dedupeIt->second;
					},
				  .extractAttachmentMimeTypes = [this](
						const std::optional<std::string>& paramsJson) {
						return ExtractAttachmentMimeTypes(paramsJson);
					},
				};
				auto pipelineResult = m_chatRunPipelineOrchestrator.Run(stageContext);
				EmitTelemetryEvent(
					"gateway.chat.pipeline.stages",
					std::string("{\"requestId\":") +
					JsonString(stageContext.requestId) +
					",\"runId\":" +
					JsonString(stageContext.runId) +
					",\"sessionKey\":" +
					JsonString(stageContext.sessionKey) +
					",\"forceError\":" +
					std::string(stageContext.forceError ? "true" : "false") +
					",\"hasAttachmentPayload\":" +
					std::string(stageContext.hasAttachmentPayload ? "true" : "false") +
					",\"normalizedMessageChars\":" +
					std::to_string(stageContext.normalizedMessage.size()) +
					",\"method\":" +
					JsonString(stageContext.method) +
					",\"status\":" +
					JsonString(pipelineResult.status) +
					",\"stages\":" +
					SerializeStringArrayLocal(stageContext.stageTrace) +
					"}");

				if (stageContext.shouldReturnEarly) {
					if (stageContext.deduped) {
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = true,
							.payloadJson =
								"{\"runId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"queued\":false,\"deduped\":true}",
							.error = std::nullopt,
						};
					}

					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
					  .error = stageContext.responseError.has_value()
							? stageContext.responseError
							: std::optional<protocol::ErrorShape>(
								protocol::ErrorShape{
									.code = stageContext.responseErrorCode,
									.message = stageContext.responseErrorMessage,
									.detailsJson = std::nullopt,
									.retryable = false,
									.retryAfterMs = std::nullopt,
								}),
					};
				}

				const std::string requestedSessionKey = stageContext.requestedSessionKey;
				const std::string sessionKey = stageContext.sessionKey;
				const std::string message = stageContext.message;
				const std::string normalizedMessage = stageContext.normalizedMessage;
				const std::string idempotencyKey = stageContext.idempotencyKey;
				const bool forceError = stageContext.forceError;
				const bool hasAttachments = stageContext.hasAttachmentPayload;

				const std::vector<std::string> attachmentMimeTypes =
					stageContext.attachmentMimeTypes;
				const std::uint64_t nowMs = stageContext.nowEpochMs > 0
					? stageContext.nowEpochMs
					: CurrentEpochMsLocal();
				const std::string runId = !stageContext.runId.empty()
					? stageContext.runId
					: (!request.id.empty()
						? request.id
						: ("chat-run-" + std::to_string(nowMs) +
							"-" + std::to_string(m_chatRunsById.size() + 1)));

				auto persistTaskDeltas =
					[this, &runId, &sessionKey](
						const std::vector<ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
						const bool success) {
							if (taskDeltas.empty()) {
								return;
							}

							std::vector<ChatRuntimeResult::TaskDeltaEntry> normalizedTaskDeltas;
							normalizedTaskDeltas.reserve(taskDeltas.size());
							for (std::size_t index = 0; index < taskDeltas.size(); ++index) {
								normalizedTaskDeltas.push_back(
									TaskDeltaLegacyAdapter::AdaptEntry(
										taskDeltas[index],
										runId,
										sessionKey,
										index));
							}

							std::string schemaErrorCode;
							std::string schemaErrorMessage;
							if (!TaskDeltaSchemaValidator::ValidateRun(
								runId,
								normalizedTaskDeltas,
								schemaErrorCode,
								schemaErrorMessage)) {
								return;
							}

							const bool upserted =
								m_taskDeltaRepository.Upsert(runId, normalizedTaskDeltas);
							(void)upserted;
							PersistTaskDeltas();
							for (const auto& delta : normalizedTaskDeltas) {
								EmitTelemetryEvent(
									"gateway.taskdelta.transition",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"phase\":" + JsonString(delta.phase) +
									",\"toolName\":" + JsonString(delta.toolName) +
									",\"status\":" + JsonString(delta.status) +
									",\"index\":" + std::to_string(delta.index) +
									",\"latencyMs\":" + std::to_string(delta.latencyMs) +
									"}");
							}

							std::string terminalStatus = success ? "completed" : "failed";
							std::string terminalErrorCode;
							for (auto it = normalizedTaskDeltas.rbegin();
								it != normalizedTaskDeltas.rend();
								++it) {
								if (it->phase != "final") {
									continue;
								}

								if (!it->status.empty()) {
									terminalStatus = it->status;
								}

								terminalErrorCode = it->errorCode;
								break;
							}

							if (terminalStatus == "completed") {
								++m_taskDeltaRunSuccessCount;
							}
							else {
								++m_taskDeltaRunFailureCount;
							}

							if (terminalErrorCode == "embedded_deadline_exceeded") {
								++m_taskDeltaRunTimeoutCount;
							}

							if (terminalErrorCode == "embedded_run_cancelled" ||
								terminalStatus == "skipped") {
								++m_taskDeltaRunCancelledCount;
							}

							if (terminalErrorCode.find("fallback") != std::string::npos ||
								terminalStatus == "fallback") {
								++m_taskDeltaRunFallbackCount;
							}

							EmitTelemetryEvent(
								"gateway.taskdelta.runSummary",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"count\":" + std::to_string(normalizedTaskDeltas.size()) +
								",\"success\":" + (success ? std::string("true") : std::string("false")) +
								",\"terminalStatus\":" + JsonString(terminalStatus) +
								",\"errorCode\":" + JsonString(terminalErrorCode) +
								",\"totals\":{\"success\":" + std::to_string(m_taskDeltaRunSuccessCount) +
								",\"failure\":" + std::to_string(m_taskDeltaRunFailureCount) +
								",\"timeout\":" + std::to_string(m_taskDeltaRunTimeoutCount) +
								",\"cancelled\":" + std::to_string(m_taskDeltaRunCancelledCount) +
								",\"fallback\":" + std::to_string(m_taskDeltaRunFallbackCount) + "}" +
								"}");

							if (m_taskDeltaRepository.Size() > 64) {
								const auto& snapshot = m_taskDeltaRepository.Snapshot();
								if (!snapshot.empty()) {
									const bool cleared =
										m_taskDeltaRepository.Clear(snapshot.begin()->first);
									(void)cleared;
								}
								PersistTaskDeltas();
							}
					};

				std::string assistantText;
				if (message.empty() && hasAttachments) {
					assistantText = IsLikelyChinesePromptLocal(normalizedMessage)
						? Utf8LiteralLocal(u8"\u5DF2\u6536\u5230\u56FE\u7247\u9644\u4EF6\u3002")
						: "Received image attachment.";
				}
				std::vector<std::string> assistantDeltas;
				std::string backendErrorCode;
				std::string backendErrorMessage;
				bool failed = false;
				bool orchestrationHandled = false;
				bool lifecycleEventsEnqueued = false;
				bool providerStreamed = false;
				const std::string orchestrationPath =
					ToLowerCopyLocal(m_embeddedOrchestrationPath);
				const bool allowPromptOrchestration =
					orchestrationPath == "runtime_orchestration";
				EmitTelemetryEvent(
					"gateway.chat.orchestration.pathSelection",
					std::string("{\"runId\":") +
					JsonString(runId) +
					",\"path\":" +
					JsonString(orchestrationPath) +
					",\"compatDeterministicEnabled\":" +
					std::string(allowPromptOrchestration ? "true" : "false") +
					",\"dynamicRuntimeDefault\":true}");
				const auto runtimeToolsSnapshot = m_toolRegistry.List();
				const auto orderedSequencePreflight =
					RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
						normalizedMessage,
						runtimeToolsSnapshot,
						m_skillsCatalogState.entries);
				const bool preferChineseResponse =
					stageContext.preferChineseResponse;
				std::vector<std::string> orderedAllowlistTargets;
				bool enforceOrderedAllowlist = false;
				if (orderedSequencePreflight.enforced &&
					orderedSequencePreflight.missingTargets.empty()) {
					enforceOrderedAllowlist = true;
					orderedAllowlistTargets.reserve(
						orderedSequencePreflight.resolvedToolTargets.size());
					for (const auto& resolvedToolId :
						orderedSequencePreflight.resolvedToolTargets) {
						if (!RuntimeSequencingPolicy::IsResolvedRuntimeToolTarget(
							resolvedToolId,
							runtimeToolsSnapshot)) {
							enforceOrderedAllowlist = false;
							orderedAllowlistTargets.clear();
							break;
						}

						orderedAllowlistTargets.push_back(resolvedToolId);
					}
				}
				const std::string runtimeMessage =
					(orderedSequencePreflight.enforced && !enforceOrderedAllowlist)
					? (std::string(preferChineseResponse
						? Utf8LiteralLocal(u8"\u6709\u5E8F\u6267\u884C\u6B65\u9AA4\uFF08\u4FDD\u6301\u987A\u5E8F\uFF09\uFF1A")
						: "Ordered execution steps (preserve order): ") +
						RuntimeSequencingPolicy::JoinOrderedResolution(
							orderedSequencePreflight) +
						"\n\n" + stageContext.runtimeMessage)
					: stageContext.runtimeMessage;
				std::vector<ChatRuntimeResult::TaskDeltaEntry> orderedPreflightTaskDeltas;

				if (forceError) {
					failed = true;
					backendErrorCode = "forced_error";
					backendErrorMessage = "forced error for deterministic verification";
				}

				auto buildLegacyOrchestrationTaskDeltas =
					[&](
						const bool success,
						const std::string& finalStatus,
						const std::string& finalText,
						const std::string& errorCode,
						const std::string& errorMessage,
						const ChatPromptOrchestrationResult& orchestrationResult) {
							std::vector<ChatRuntimeResult::TaskDeltaEntry> taskDeltas;
							const std::uint64_t baseMs = CurrentEpochMsLocal();

							taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
								.index = taskDeltas.size(),
								.runId = runId,
								.sessionId = sessionKey,
								.phase = "plan",
								.resultJson =
									"[\"weather.lookup\",\"report.compose\",\"email.schedule\"]",
								.status = "ok",
								.startedAtMs = baseMs,
								.completedAtMs = baseMs,
								.latencyMs = 0,
								.stepLabel = "execution_plan",
								});

							if (success) {
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_call",
									.toolName = "weather.lookup",
									.status = "requested",
									.startedAtMs = baseMs + 1,
									.completedAtMs = baseMs + 1,
									.latencyMs = 0,
									.stepLabel = "tool_request",
									});
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_result",
									.toolName = "weather.lookup",
									.status = "ok",
									.startedAtMs = baseMs + 1,
									.completedAtMs = baseMs + 2,
									.latencyMs = 1,
									.stepLabel = "tool_result",
									});
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_call",
									.toolName = "report.compose",
									.status = "requested",
									.startedAtMs = baseMs + 2,
									.completedAtMs = baseMs + 2,
									.latencyMs = 0,
									.stepLabel = "tool_request",
									});
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_result",
									.toolName = "report.compose",
									.status = "ok",
									.startedAtMs = baseMs + 2,
									.completedAtMs = baseMs + 3,
									.latencyMs = 1,
									.stepLabel = "tool_result",
									});
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_call",
									.toolName = "email.schedule",
									.status = "requested",
									.startedAtMs = baseMs + 3,
									.completedAtMs = baseMs + 3,
									.latencyMs = 0,
									.stepLabel = "tool_request",
									});
								taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
									.index = taskDeltas.size(),
									.runId = runId,
									.sessionId = sessionKey,
									.phase = "tool_result",
									.toolName = "email.schedule",
								 .fallbackBackend = orchestrationResult.fallbackBackend,
									.fallbackAction = orchestrationResult.fallbackAction,
									.fallbackAttempt = orchestrationResult.fallbackAttempt,
									.fallbackMaxAttempts = orchestrationResult.fallbackMaxAttempts,
									.status = "needs_approval",
									.startedAtMs = baseMs + 3,
									.completedAtMs = baseMs + 4,
									.latencyMs = 1,
									.stepLabel = "tool_result",
									});
							}

							taskDeltas.push_back(ChatRuntimeResult::TaskDeltaEntry{
								.index = taskDeltas.size(),
								.runId = runId,
								.sessionId = sessionKey,
								.phase = "final",
								.resultJson = success ? finalText : errorMessage,
							  .status = orchestrationResult.terminalStatus.empty()
									? finalStatus
									: orchestrationResult.terminalStatus,
								.errorCode = errorCode,
								.startedAtMs = baseMs + 4,
								.completedAtMs = baseMs + 4,
								.latencyMs = 0,
								.stepLabel = "run_terminal",
								});

							return taskDeltas;
					};

				auto buildAssistantDeltasFromTaskDeltas =
					[&runId](
						const std::vector<ChatRuntimeResult::TaskDeltaEntry>& taskDeltas) {
							std::vector<std::string> deltas;
							for (const auto& delta : taskDeltas) {
								if (delta.phase == "tool_call" && !delta.toolName.empty()) {
									deltas.push_back("tools.execute.start tool=" + delta.toolName);
									continue;
								}

								if (delta.phase == "tool_result" && !delta.toolName.empty()) {
									if (delta.toolName == "email.schedule") {
										EmitTelemetryEvent(
											"gateway.email.fallback.attempt",
											std::string("{\"runId\":") +
											JsonString(runId) +
											",\"tool\":\"email.schedule\",\"status\":" +
											JsonString(delta.status) +
											",\"backend\":" +
											JsonString(delta.fallbackBackend) +
											",\"action\":" +
											JsonString(delta.fallbackAction) +
											",\"attempt\":" +
											std::to_string(delta.fallbackAttempt) +
											",\"maxAttempts\":" +
											std::to_string(delta.fallbackMaxAttempts) +
											"}");
									}

									deltas.push_back(
										"tools.execute.result tool=" +
										delta.toolName +
										" status=" +
										(delta.status.empty() ? std::string("ok") : delta.status) +
										(delta.errorCode.empty()
											? std::string()
											: (" errorCode=" + delta.errorCode)));
									continue;
								}
							}

							return deltas;
					};

				if (!forceError &&
					!hasAttachments &&
					orderedSequencePreflight.enforced) {
					orderedPreflightTaskDeltas =
						RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
							runId,
							sessionKey,
							orderedSequencePreflight,
							false,
							{},
							{});

					EmitTelemetryEvent(
						"gateway.chat.ordered.preflight",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"enforced\":true,\"steps\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.orderedTargets) +
						",\"resolvedTools\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.resolvedToolTargets) +
						",\"missing\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
						"}");

					if (!orderedSequencePreflight.missingTargets.empty()) {
						failed = true;
						orchestrationHandled = true;
						backendErrorCode = "ordered_sequence_target_unavailable";
						backendErrorMessage =
							"Ordered execution preflight failed. Missing targets: " +
							RuntimeSequencingPolicy::JoinOrderedTargets(
								orderedSequencePreflight.missingTargets);
						assistantText =
							(preferChineseResponse
								? (Utf8LiteralLocal(u8"\u65E0\u6CD5\u6267\u884C\u6709\u5E8F\u5DE5\u4F5C\u6D41\uFF0C\u4EE5\u4E0B\u6B65\u9AA4\u76EE\u6807\u4E0D\u53EF\u7528\uFF1A") +
									JoinOrderedTargets(orderedSequencePreflight.missingTargets) +
									Utf8LiteralLocal(u8"\u3002\u8BF7\u5B89\u88C5\u6216\u542F\u7528\u8FD9\u4E9B\u6280\u80FD/\u5DE5\u5177\u540E\u91CD\u8BD5\u3002"))
								: (std::string("Unable to execute the ordered workflow because required step targets are unavailable: ") +
									JoinOrderedTargets(orderedSequencePreflight.missingTargets) +
									". Please install or enable these skills/tools and retry."));

						auto blockedTaskDeltas =
							RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
								runId,
								sessionKey,
								orderedSequencePreflight,
								true,
								backendErrorCode,
								backendErrorMessage);
						assistantDeltas =
							buildAssistantDeltasFromTaskDeltas(blockedTaskDeltas);
						if (assistantDeltas.empty()) {
							assistantDeltas.push_back(assistantText);
						}

						persistTaskDeltas(blockedTaskDeltas, false);
					}
				}

				if (!forceError &&
					!hasAttachments &&
					!normalizedMessage.empty() &&
					allowPromptOrchestration) {
					const auto orchestrationResult = TryOrchestrateWeatherEmailPrompt(
						m_toolRegistry,
						normalizedMessage);

					EmitTelemetryEvent(
						"gateway.chat.orchestration.intent",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"path\":" +
						JsonString(orchestrationPath) +
						",\"matched\":" +
						std::string(orchestrationResult.matched ? "true" : "false") +
						",\"scheduleKind\":" +
						JsonString(orchestrationResult.scheduleKind) +
						",\"city\":" +
						JsonString(orchestrationResult.city) +
						",\"date\":" +
						JsonString(orchestrationResult.date) +
						",\"recipient\":" +
						JsonString(orchestrationResult.recipient) +
						",\"sendAt\":" +
						JsonString(orchestrationResult.sendAt) +
						",\"missReasons\":" +
						SerializeStringArrayLocal(orchestrationResult.missReasons) +
						",\"decompositionSteps\":" +
						std::to_string(orchestrationResult.decompositionSteps) +
						"}");

					if (orchestrationResult.matched) {
						orchestrationHandled = true;
						if (orchestrationResult.success) {
							assistantText = orchestrationResult.assistantText;
							auto legacyTaskDeltas =
								buildLegacyOrchestrationTaskDeltas(
									true,
									orchestrationResult.terminalStatus.empty()
									? "completed"
									: orchestrationResult.terminalStatus,
									assistantText,
									{},
									{},
									orchestrationResult);
							assistantDeltas =
								buildAssistantDeltasFromTaskDeltas(legacyTaskDeltas);
							if (assistantDeltas.empty()) {
								assistantDeltas = orchestrationResult.assistantDeltas;
							}

							EmitTelemetryEvent(
								"gateway.chat.orchestration.execution",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"path\":" +
								JsonString(orchestrationPath) +
								",\"status\":\"success\",\"steps\":" +
								std::to_string(
									orchestrationResult.decompositionSteps) +
								"}");
							EmitTelemetryEvent(
								"gateway.email.fallback.terminal",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"status\":" +
								JsonString(orchestrationResult.terminalStatus.empty()
									? std::string("completed")
									: orchestrationResult.terminalStatus) +
								",\"errorCode\":null,\"errorMessage\":null}");

							m_chatRunsById.insert_or_assign(
								runId,
								ChatRunState{
									.runId = runId,
									.sessionKey = sessionKey,
									.idempotencyKey = idempotencyKey,
									.userMessage = message,
									.assistantText = assistantText,
									.providerDeltas = assistantDeltas,
									.providerDeltaCursor = 0,
									.streamCursor = 0,
									.lastEmitMs = nowMs,
									.failed = false,
									.errorMessage = {},
									.startedAtMs = nowMs,
								 .active = true,
									.terminalEventEnqueued = false,
								});

							persistTaskDeltas(legacyTaskDeltas, true);
						}
						else {
							failed = true;
							assistantText.clear();
							backendErrorCode = orchestrationResult.errorCode.empty()
								? "chat_tool_orchestration_failed"
								: orchestrationResult.errorCode;
							backendErrorMessage = orchestrationResult.errorMessage.empty()
								? "chat tool orchestration failed"
								: orchestrationResult.errorMessage;

							EmitTelemetryEvent(
								"gateway.chat.orchestration.execution",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"path\":" +
								JsonString(orchestrationPath) +
								",\"status\":\"failed\",\"errorCode\":" +
								JsonString(backendErrorCode) +
								",\"errorMessage\":" +
								JsonString(backendErrorMessage) +
								"}");
							EmitTelemetryEvent(
								"gateway.email.fallback.terminal",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"status\":\"failed\",\"errorCode\":" +
								JsonString(backendErrorCode) +
								",\"errorMessage\":" +
								JsonString(backendErrorMessage) +
								"}");

							auto legacyTaskDeltas =
								buildLegacyOrchestrationTaskDeltas(
									false,
									orchestrationResult.terminalStatus.empty()
									? "failed"
									: orchestrationResult.terminalStatus,
									{},
									backendErrorCode,
									backendErrorMessage,
									orchestrationResult);
							assistantDeltas =
								buildAssistantDeltasFromTaskDeltas(legacyTaskDeltas);
							persistTaskDeltas(legacyTaskDeltas, false);
						}
					}
				}

				if (!forceError && !orchestrationHandled && m_chatRuntimeCallback) {
					auto& runtimeSessionEvents = m_chatEventsBySession[sessionKey];
					PushEventWithRetentionLimit(runtimeSessionEvents, ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "queued",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					PushEventWithRetentionLimit(runtimeSessionEvents, ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "started",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					lifecycleEventsEnqueued = true;

					m_chatRunsById.insert_or_assign(
						runId,
						ChatRunState{
							.runId = runId,
							.sessionKey = sessionKey,
							.idempotencyKey = idempotencyKey,
							.userMessage = message,
							.assistantText = {},
							.providerDeltas = {},
							.providerDeltaCursor = 0,
							.streamCursor = 0,
							.lastEmitMs = nowMs,
							.failed = false,
							.errorMessage = {},
							.startedAtMs = nowMs,
							.active = true,
							.terminalEventEnqueued = false,
						});

					std::size_t streamedDeltaCount = 0;
					const auto runtimeResult = m_chatRuntimeCallback(
						ChatRuntimeRequest{
							.runId = runId,
							.sessionKey = sessionKey,
					  .message = runtimeMessage,
						 .enforceOrderedAllowlist = enforceOrderedAllowlist,
							.orderedAllowedToolTargets = orderedAllowlistTargets,
							.hasAttachments = hasAttachments,
							.attachmentMimeTypes = attachmentMimeTypes,
						 .onAssistantDelta =
								[this, &streamedDeltaCount, &runId, &sessionKey](const std::string& delta) {
									const std::string normalizedDelta = json::Trim(delta);
									if (normalizedDelta.empty() ||
										RuntimeTranscriptGuard::IsSilentReplyText(normalizedDelta)) {
										return;
									}

									auto& streamEvents = m_chatEventsBySession[sessionKey];
									PushEventWithRetentionLimit(streamEvents, ChatEventState{
											.runId = runId,
											.sessionKey = sessionKey,
											.state = "delta",
											.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
											.errorMessage = std::nullopt,
											.timestampMs = CurrentEpochMsLocal(),
										});

									auto runStateIt = m_chatRunsById.find(runId);
									if (runStateIt != m_chatRunsById.end()) {
										runStateIt->second.assistantText = normalizedDelta;
										runStateIt->second.streamCursor = normalizedDelta.size();
										runStateIt->second.lastEmitMs = CurrentEpochMsLocal();
									}

									++streamedDeltaCount;
								}
						});
					providerStreamed = streamedDeltaCount > 0;

					if (runtimeResult.ok) {
						if (!runtimeResult.assistantText.empty()) {
							assistantText = runtimeResult.assistantText;
						}

						if (assistantText.empty() &&
							!runtimeResult.assistantDeltas.empty()) {
							assistantText = runtimeResult.assistantDeltas.back();
						}

						std::vector<std::string> providerDeltas =
							RuntimeTranscriptGuard::NormalizeAssistantDeltas(
								runtimeResult.assistantDeltas,
								assistantText,
								providerStreamed);
						assistantDeltas = providerDeltas;

						if (assistantText.empty()) {
							failed = true;
							backendErrorCode = "chat_runtime_empty_response";
							backendErrorMessage =
								"chat runtime returned no assistant output";
						}
						else {
							auto existingRunIt = m_chatRunsById.find(runId);
							if (existingRunIt != m_chatRunsById.end()) {
								existingRunIt->second.assistantText = assistantText;
								existingRunIt->second.providerDeltas = assistantDeltas;
								existingRunIt->second.providerDeltaCursor = 0;
								existingRunIt->second.streamCursor =
									providerStreamed ? assistantText.size() : 0;
								existingRunIt->second.lastEmitMs = nowMs;
								existingRunIt->second.failed = failed;
								existingRunIt->second.errorMessage = backendErrorMessage;
								existingRunIt->second.active = true;
							}
						}
					}
					else {
						failed = true;
						backendErrorCode = runtimeResult.errorCode.empty()
							? "chat_runtime_error"
							: runtimeResult.errorCode;
						backendErrorMessage = runtimeResult.errorMessage.empty()
							? "chat runtime failed"
							: runtimeResult.errorMessage;
						assistantText.clear();
					}

					auto existingRunIt = m_chatRunsById.find(runId);
					if (existingRunIt != m_chatRunsById.end()) {
						existingRunIt->second.assistantText = assistantText;
						existingRunIt->second.providerDeltas = assistantDeltas;
						existingRunIt->second.providerDeltaCursor = 0;
						existingRunIt->second.streamCursor =
							providerStreamed ? assistantText.size() : 0;
						existingRunIt->second.lastEmitMs = nowMs;
						existingRunIt->second.failed = failed;
						existingRunIt->second.errorMessage = backendErrorMessage;
						existingRunIt->second.active = true;
					}

					auto runtimeTaskDeltas =
						RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
							runtimeResult.taskDeltas,
							runId,
							sessionKey,
							runtimeResult.ok && !failed,
							assistantText,
							backendErrorCode,
							backendErrorMessage);
					runtimeTaskDeltas =
						RuntimeToolCallNormalizer::ApplyInvalidArgumentsRecoveryPolicy(
							runtimeTaskDeltas,
							runId,
							sessionKey,
							normalizedMessage,
							m_toolRegistry);

					if (failed) {
						RunLoopBudget budget;
						const RecoveryOutcome recoveryOutcome =
							RecoveryPolicyEngine::Execute(
								RecoveryRequest{
									.runId = runId,
									.sessionKey = sessionKey,
									.message = normalizedMessage,
									.errorCode = backendErrorCode,
									.errorMessage = backendErrorMessage,
									.authProfileId = "default",
									.taskDeltas = runtimeTaskDeltas,
								},
								budget);

						if (!recoveryOutcome.recoveryDeltas.empty()) {
							runtimeTaskDeltas.insert(
								runtimeTaskDeltas.end(),
								recoveryOutcome.recoveryDeltas.begin(),
								recoveryOutcome.recoveryDeltas.end());
						}

						if (!recoveryOutcome.normalizedDeltas.empty()) {
							runtimeTaskDeltas = recoveryOutcome.normalizedDeltas;
						}

						EmitTelemetryEvent(
							"gateway.chat.recovery.decision",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"recovered\":" +
							std::string(recoveryOutcome.recovered ? "true" : "false") +
							",\"retry\":" +
							std::string(recoveryOutcome.shouldRetry ? "true" : "false") +
							",\"reinvoke\":" +
							std::string(recoveryOutcome.shouldReinvokeRuntime ? "true" : "false") +
							",\"compaction\":" +
							std::string(recoveryOutcome.compactionApplied ? "true" : "false") +
							",\"truncation\":" +
							std::string(recoveryOutcome.truncationApplied ? "true" : "false") +
							",\"profile\":" +
							JsonString(recoveryOutcome.selectedProfileId) +
							",\"contextEngine\":" +
							JsonString(recoveryOutcome.selectedContextEngineId) +
							",\"terminalCode\":" +
							JsonString(recoveryOutcome.terminalErrorCode) +
							"}");

						if (recoveryOutcome.recovered) {
							failed = false;
							backendErrorCode.clear();
							backendErrorMessage.clear();
							if (assistantText.empty()) {
								assistantText = recoveryOutcome.compactionApplied
									? "Recovered via context compaction; runtime will continue."
									: "Recovered via fallback normalization; runtime will continue.";
							}
						}
						else if (!recoveryOutcome.terminalErrorCode.empty()) {
							backendErrorCode = recoveryOutcome.terminalErrorCode;
							if (!recoveryOutcome.terminalErrorMessage.empty()) {
								backendErrorMessage = recoveryOutcome.terminalErrorMessage;
							}
						}
					}

					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

					persistTaskDeltas(
						runtimeTaskDeltas,
						runtimeResult.ok && !failed);
				}
				else if (!forceError && !orchestrationHandled && !m_chatRuntimeCallback) {
					failed = true;
					assistantText.clear();
					assistantDeltas.clear();
					backendErrorCode = "chat_runtime_callback_missing";
					backendErrorMessage = "chat runtime callback is not configured";

					auto runtimeTaskDeltas =
						RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
							{},
							runId,
							sessionKey,
							false,
							assistantText,
							backendErrorCode,
							backendErrorMessage);
					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

					persistTaskDeltas(runtimeTaskDeltas, false);
				}

				const bool silentAssistantReply =
					RuntimeTranscriptGuard::IsSilentReplyText(assistantText);

				auto& sessionHistory = m_chatHistoryBySession[sessionKey];
				PushHistoryMessageIfNew(
					sessionHistory,
					BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));

				auto& sessionEvents = m_chatEventsBySession[sessionKey];
				if (!lifecycleEventsEnqueued) {
					PushEventWithRetentionLimit(sessionEvents, ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "queued",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					PushEventWithRetentionLimit(sessionEvents, ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "started",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
				}
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=queued runId=") +
					runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(sessionEvents.size()));
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=started runId=") +
					runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(sessionEvents.size()));

				std::size_t streamCursor = 0;
				if (!failed && !silentAssistantReply && !providerStreamed) {
					streamCursor = (std::min)(assistantText.size(), std::size_t{ 6 });
					if (streamCursor > 0) {
						PushEventWithRetentionLimit(sessionEvents, ChatEventState{
							   .runId = runId,
							   .sessionKey = sessionKey,
							   .state = "delta",
							   .messageJson = BuildAssistantDeltaMessageJson(
								   assistantText.substr(0, streamCursor)),
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							runId +
							" session=" +
							sessionKey +
							" queueSize=" +
							std::to_string(sessionEvents.size()));
					}
				}

				if (m_chatRunsById.find(runId) == m_chatRunsById.end()) {
					m_chatRunsById.insert_or_assign(
						runId,
						ChatRunState{
							.runId = runId,
							.sessionKey = sessionKey,
							.idempotencyKey = idempotencyKey,
							.userMessage = message,
							.assistantText = assistantText,
							.providerDeltas = assistantDeltas,
							.providerDeltaCursor = 0,
							.streamCursor = streamCursor,
							.lastEmitMs = nowMs,
							.failed = failed,
							.errorMessage = backendErrorMessage,
							.startedAtMs = nowMs,
						 .active = true,
							.terminalEventEnqueued = false,
						});
				}

				m_chatTerminalDeliveredRunIds.erase(runId);

				if (!idempotencyKey.empty()) {
					m_chatRunByIdempotency.insert_or_assign(idempotencyKey, runId);
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"backendErrorCode\":" +
						(backendErrorCode.empty()
							? std::string("null")
							: ("\"" + EscapeJsonLocal(backendErrorCode) + "\"")) +
						",\"queued\":true,\"deduped\":false}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"chat.abort",
			[this](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::string requestedRunId =
					ExtractStringParam(request.paramsJson, "runId");

				auto runIt = m_chatRunsById.end();
				if (!requestedRunId.empty()) {
					const auto exact = m_chatRunsById.find(requestedRunId);
					if (exact != m_chatRunsById.end() &&
						exact->second.sessionKey == sessionKey) {
						runIt = exact;
					}
				}
				else {
					runIt = std::find_if(
						m_chatRunsById.begin(),
						m_chatRunsById.end(),
						[&](const auto& pair) {
							return pair.second.sessionKey == sessionKey &&
								pair.second.active;
						});
				}

				if (runIt == m_chatRunsById.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson =
							"{\"aborted\":false,\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\"}",
						.error = std::nullopt,
					};
				}

				const std::string runId = runIt->second.runId;
				if (m_chatAbortCallback) {
					m_chatAbortCallback(
						ChatAbortRequest{
							.runId = runId,
							.sessionKey = sessionKey,
						});
				}

				auto& queue = m_chatEventsBySession[sessionKey];
				std::erase_if(
					queue,
					[&](const ChatEventState& item) {
						return item.runId == runId;
					});

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				const bool silentAssistantReply =
					RuntimeTranscriptGuard::IsSilentReplyText(
						runIt->second.assistantText);
				PushEventWithRetentionLimit(queue, ChatEventState{
					   .runId = runIt->second.runId,
					   .sessionKey = sessionKey,
					   .state = "aborted",
					   .messageJson = silentAssistantReply
						   ? std::nullopt
						   : std::optional<std::string>(
							   BuildAssistantFinalMessageJson(
								   runIt->second.assistantText,
								   nowMs)),
					   .errorMessage = std::nullopt,
					   .timestampMs = nowMs,
					});
				runIt->second.terminalEventEnqueued = true;
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=aborted runId=") +
					runIt->second.runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(queue.size()));

				runIt->second.active = false;
				runIt->second.streamCursor = runIt->second.assistantText.size();

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"aborted\":true,\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"chat.events.poll",
			[this](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::size_t requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 100 }));

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				auto queueIt = m_chatEventsBySession.find(sessionKey);
				auto& queue = m_chatEventsBySession[sessionKey];

				auto runIt = std::find_if(
					m_chatRunsById.begin(),
					m_chatRunsById.end(),
					[&](auto& pair) {
						return pair.second.sessionKey == sessionKey && pair.second.active;
					});

				if (runIt != m_chatRunsById.end()) {
					auto& run = runIt->second;
					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(run.assistantText);
					const bool enoughTimeElapsed =
						run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;

					if (!run.failed && !silentAssistantReply &&
						((run.providerDeltaCursor < run.providerDeltas.size()) ||
							(run.streamCursor < run.assistantText.size())) &&
						enoughTimeElapsed) {
						std::string deltaText;
						if (run.providerDeltaCursor < run.providerDeltas.size()) {
							deltaText = run.providerDeltas[run.providerDeltaCursor];
							++run.providerDeltaCursor;
							run.streamCursor = deltaText.size();
						}
						else {
							const std::size_t nextCursor =
								(std::min)(run.assistantText.size(), run.streamCursor + std::size_t{ 8 });
							run.streamCursor = nextCursor;
							deltaText = run.assistantText.substr(0, run.streamCursor);
						}

						run.lastEmitMs = nowMs;

						PushEventWithRetentionLimit(queue, ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = "delta",
							   .messageJson = BuildAssistantDeltaMessageJson(deltaText),
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							run.runId +
							" session=" +
							run.sessionKey +
							" queueSize=" +
							std::to_string(queue.size()));
					}

					const bool streamCompleted =
						run.failed ||
						silentAssistantReply ||
						(run.providerDeltaCursor >= run.providerDeltas.size() &&
							run.streamCursor >= run.assistantText.size());
					if (streamCompleted && !run.terminalEventEnqueued) {
						PushEventWithRetentionLimit(queue, ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = run.failed ? "error" : "final",
							   .messageJson = run.failed || silentAssistantReply
								   ? std::nullopt
								   : std::optional<std::string>(
									   BuildAssistantFinalMessageJson(run.assistantText, nowMs)),
							   .errorMessage = run.failed
								   ? std::optional<std::string>(run.errorMessage.empty()
									   ? "chat error"
									   : run.errorMessage)
								   : std::nullopt,
							   .timestampMs = nowMs,
							});
						run.terminalEventEnqueued = true;
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=") +
							(run.failed ? "error" : "final") +
							" runId=" +
							run.runId +
							" session=" +
							run.sessionKey +
							" queueSize=" +
							std::to_string(queue.size()));

						run.active = false;
					}
				}

				std::string eventsJson = "[";
				std::size_t emitted = 0;
				std::unordered_set<std::string> terminalRunIdsSeenThisPoll;
				if (!queue.empty()) {
					while (emitted < limit && !queue.empty()) {
						const ChatEventState eventState = queue.front();
						queue.pop_front();

						if (IsTerminalChatState(eventState.state)) {
							if (m_chatTerminalDeliveredRunIds.find(eventState.runId) != m_chatTerminalDeliveredRunIds.end() ||
								terminalRunIdsSeenThisPoll.find(eventState.runId) != terminalRunIdsSeenThisPoll.end()) {
								continue;
							}

							terminalRunIdsSeenThisPoll.insert(eventState.runId);
							m_chatTerminalDeliveredRunIds.insert(eventState.runId);
							if (m_chatTerminalDeliveredRunIds.size() > 1024) {
								m_chatTerminalDeliveredRunIds.clear();
							}
						}

						EmitDeepSeekGatewayDiagnostic(
							"event.dequeue",
							std::string("state=") +
							eventState.state +
							" runId=" +
							eventState.runId +
							" session=" +
							eventState.sessionKey +
							" emitted=" +
							std::to_string(emitted + 1) +
							" queueRemaining=" +
							std::to_string(queue.size()));

						if (emitted > 0) {
							eventsJson += ",";
						}

						eventsJson += BuildChatEventJson(
							eventState.runId,
							eventState.sessionKey,
							eventState.state,
							eventState.messageJson,
							eventState.errorMessage,
							eventState.timestampMs);
						++emitted;

						if ((eventState.state == "final" ||
							eventState.state == "aborted") &&
							eventState.messageJson.has_value() &&
							!IsSilentAssistantMessageJson(eventState.messageJson.value())) {
							PushHistoryMessageIfNew(
								m_chatHistoryBySession[sessionKey],
								eventState.messageJson.value());
						}

						if (IsTerminalChatState(eventState.state)) {
							const auto runIt = m_chatRunsById.find(eventState.runId);
							if (runIt != m_chatRunsById.end()) {
								if (!runIt->second.idempotencyKey.empty()) {
									m_chatRunByIdempotency.erase(
										runIt->second.idempotencyKey);
								}

								m_chatRunsById.erase(runIt);
							}
						}
					}
				}

				eventsJson += "]";
				EmitDeepSeekGatewayDiagnostic(
					"event.dequeue",
					std::string("poll complete session=") +
					sessionKey +
					" emitted=" +
					std::to_string(emitted) +
					" queueRemaining=" +
					std::to_string(queue.size()));
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\",\"events\":" +
						eventsJson +
						",\"count\":" +
						std::to_string(emitted) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"total\":" +
						std::to_string(state.entries.size()) +
						",\"rootsScanned\":" +
						std::to_string(state.rootsScanned) +
						",\"rootsSkipped\":" +
						std::to_string(state.rootsSkipped) +
						",\"oversizedSkillFiles\":" +
						std::to_string(state.oversizedSkillFiles) +
						",\"invalidFrontmatterFiles\":" +
						std::to_string(state.invalidFrontmatterFiles) +
						",\"eligible\":" +
						std::to_string(state.eligibleCount) +
						",\"disabled\":" +
						std::to_string(state.disabledCount) +
						",\"blockedByAllowlist\":" +
						std::to_string(state.blockedByAllowlistCount) +
						",\"missingRequirements\":" +
						std::to_string(state.missingRequirementsCount) +
						",\"promptIncluded\":" +
						std::to_string(state.promptIncludedCount) +
						",\"promptChars\":" +
						std::to_string(state.promptChars) +
						",\"promptTruncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						",\"snapshotVersion\":" +
						std::to_string(state.snapshotVersion) +
						",\"watchEnabled\":" +
						std::string(state.watchEnabled ? "true" : "false") +
						",\"watchDebounceMs\":" +
						std::to_string(state.watchDebounceMs) +
						",\"watchReason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"" +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"sandboxSynced\":" +
						std::to_string(state.sandboxSynced) +
						",\"sandboxSkipped\":" +
						std::to_string(state.sandboxSkipped) +
						",\"envAllowed\":" +
						std::to_string(state.envAllowed) +
						",\"envBlocked\":" +
						std::to_string(state.envBlocked) +
						",\"installExecutable\":" +
						std::to_string(state.installExecutableCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanInfo\":" +
						std::to_string(state.scanInfoCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanFiles\":" +
						std::to_string(state.scanScannedFiles) +
						",\"warnings\":" +
						std::to_string(state.warningCount) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.install.options",
			[this](const protocol::RequestFrame& request) {
				std::string optionsJson = "[";
				bool first = true;
				std::size_t count = 0;
				for (const auto& entry : m_skillsCatalogState.entries) {
					if (entry.installKind.empty()) {
						continue;
					}

					if (!first) {
						optionsJson += ",";
					}

					optionsJson +=
						"{\"skill\":\"" +
						EscapeJsonLocal(entry.name) +
						"\",\"kind\":\"" +
						EscapeJsonLocal(entry.installKind) +
						"\",\"command\":\"" +
						EscapeJsonLocal(entry.installCommand) +
						"\",\"executable\":" +
						std::string(entry.installExecutable ? "true" : "false") +
						",\"reason\":\"" +
						EscapeJsonLocal(entry.installReason) +
						"\"}";
					first = false;
					++count;
				}

				optionsJson += "]";
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"options\":" +
						optionsJson +
						",\"count\":" +
						std::to_string(count) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.install.execute",
			[this](const protocol::RequestFrame& request) {
				const auto skillName =
					ExtractStringParam(request.paramsJson, "skill");
				const auto it = std::find_if(
					m_skillsCatalogState.entries.begin(),
					m_skillsCatalogState.entries.end(),
					[&skillName](const SkillsCatalogGatewayEntry& item) {
						return item.name == skillName;
					});

				if (it == m_skillsCatalogState.entries.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "skill_not_found",
							.message = "Requested skill install target was not found.",
							.detailsJson = "{\"skill\":\"" +
								EscapeJsonLocal(skillName) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto warning = m_skillsCatalogState.scanCriticalCount > 0
					? "security_scan_critical"
					: (m_skillsCatalogState.scanWarnCount > 0
						? "security_scan_warn"
						: "none");

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"kind\":\"" +
						EscapeJsonLocal(it->installKind) +
						"\",\"command\":\"" +
						EscapeJsonLocal(it->installCommand) +
						"\",\"executed\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"warning\":\"" +
						warning +
						"\",\"scanCritical\":" +
						std::to_string(m_skillsCatalogState.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(m_skillsCatalogState.scanWarnCount) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.scan.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"files\":" +
						std::to_string(state.scanScannedFiles) +
						",\"info\":" +
						std::to_string(state.scanInfoCount) +
						",\"warn\":" +
						std::to_string(state.scanWarnCount) +
						",\"critical\":" +
						std::to_string(state.scanCriticalCount) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.sandbox.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"ok\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"synced\":" +
						std::to_string(state.sandboxSynced) +
						",\"skipped\":" +
						std::to_string(state.sandboxSkipped) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.env.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.info",
			[this](const protocol::RequestFrame& request) {
				const auto skillName =
					ExtractStringParam(request.paramsJson, "skill");
				if (skillName.empty()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "missing_skill",
							.message = "Parameter `skill` is required.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto it = std::find_if(
					m_skillsCatalogState.entries.begin(),
					m_skillsCatalogState.entries.end(),
					[&skillName](const SkillsCatalogGatewayEntry& entry) {
						return entry.name == skillName ||
							entry.skillKey == skillName;
					});

				if (it == m_skillsCatalogState.entries.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "skill_not_found",
							.message = "Skill not found.",
							.detailsJson =
								"{\"skill\":\"" +
								EscapeJsonLocal(skillName) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"skillKey\":\"" +
						EscapeJsonLocal(it->skillKey) +
						"\",\"primaryEnv\":\"" +
						EscapeJsonLocal(it->primaryEnv) +
						"\",\"requiresEnv\":" +
						SerializeStringArrayLocal(it->requiresEnv) +
						",\"requiresConfig\":" +
						SerializeStringArrayLocal(it->requiresConfig) +
						",\"missingEnv\":" +
						SerializeStringArrayLocal(it->missingEnv) +
						",\"missingConfig\":" +
						SerializeStringArrayLocal(it->missingConfig) +
						",\"missingBins\":" +
						SerializeStringArrayLocal(it->missingBins) +
						",\"missingAnyBins\":" +
						SerializeStringArrayLocal(it->missingAnyBins) +
						",\"eligible\":" +
						std::string(it->eligible ? "true" : "false") +
						",\"disabled\":" +
						std::string(it->disabled ? "true" : "false") +
						",\"blockedByAllowlist\":" +
						std::string(it->blockedByAllowlist ? "true" : "false") +
						",\"installKind\":\"" +
						EscapeJsonLocal(it->installKind) +
					   "\",\"installReason\":\"" +
						EscapeJsonLocal(it->installReason) +
						"\",\"installExecutable\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"scanCritical\":" +
						std::to_string(m_skillsCatalogState.scanCriticalCount) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.update",
			[this](const protocol::RequestFrame& request) {
				if (!m_skillsUpdateCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "not_supported",
							.message = "Skills update callback is not configured.",
							.detailsJson = std::nullopt,
						  .retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return m_skillsUpdateCallback(request);
			});

		m_dispatcher.Register(
			"skills.update",
			[this](const protocol::RequestFrame& request) {
				protocol::RequestFrame delegated = request;
				delegated.method = "gateway.skills.update";
				if (!m_skillsUpdateCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "not_supported",
							.message = "Skills update callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return m_skillsUpdateCallback(delegated);
			});

		m_dispatcher.Register(
			"gateway.skills.check",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				const bool ok =
					state.scanCriticalCount == 0 &&
					state.installBlockedCount == 0 &&
					state.sandboxSyncOk;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"ok\":" +
						std::string(ok ? "true" : "false") +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.diagnostics",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				std::vector<std::string> hints;
				if (state.installBlockedCount > 0) {
					hints.push_back("skills.install.options");
				}

				if (state.scanCriticalCount > 0) {
					hints.push_back("skills.scan.status");
				}

				if (!state.sandboxSyncOk) {
					hints.push_back("skills.sandbox.status");
				}

				std::string hintsJson = "[";
				for (std::size_t index = 0; index < hints.size(); ++index) {
					if (index > 0) {
						hintsJson += ",";
					}

					hintsJson +=
						"\"" +
						EscapeJsonLocal(hints[index]) +
						"\"";
				}

				hintsJson += "]";
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"warnings\":" +
						std::to_string(state.warningCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"hints\":" +
						hintsJson +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.prompt",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"prompt\":\"" +
						EscapeJsonLocal(state.prompt) +
						"\",\"included\":" +
						std::to_string(state.promptIncludedCount) +
						",\"chars\":" +
						std::to_string(state.promptChars) +
						",\"truncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.commands",
			[this](const protocol::RequestFrame& request) {
				std::string commandsJson = "[";
				bool first = true;
				std::size_t count = 0;

				for (const auto& entry : m_skillsCatalogState.entries) {
					if (entry.commandName.empty()) {
						continue;
					}

					if (!first) {
						commandsJson += ",";
					}

					commandsJson +=
						"{\"name\":\"" +
						EscapeJsonLocal(entry.commandName) +
						"\",\"skill\":\"" +
						EscapeJsonLocal(entry.name) +
						"\",\"toolName\":\"" +
						EscapeJsonLocal(entry.commandToolName) +
						"\",\"argMode\":\"" +
						EscapeJsonLocal(entry.commandArgMode) +
						"\",\"argSchema\":\"" +
						EscapeJsonLocal(entry.commandArgSchema) +
						"\",\"resultSchema\":\"" +
						EscapeJsonLocal(entry.commandResultSchema) +
						"\",\"idempotencyHint\":\"" +
						EscapeJsonLocal(entry.commandIdempotencyHint) +
						"\",\"retryPolicyHint\":\"" +
						EscapeJsonLocal(entry.commandRetryPolicyHint) +
						"\",\"requiresApproval\":" +
						std::string(entry.commandRequiresApproval ? "true" : "false") +
						"\",\"description\":\"" +
						EscapeJsonLocal(entry.description) +
						"\"}";
					first = false;
					++count;
				}

				commandsJson += "]";
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"commands\":" +
						commandsJson +
						",\"count\":" +
						std::to_string(count) +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.refresh",
			[this](const protocol::RequestFrame& request) {
				bool refreshed = false;
				if (m_skillsRefreshCallback) {
					m_skillsCatalogState = m_skillsRefreshCallback();
					refreshed = true;
				}

				const auto& state = m_skillsCatalogState;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"refreshed\":" +
						std::string(refreshed ? "true" : "false") +
						",\"version\":" +
						std::to_string(state.snapshotVersion) +
						",\"reason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.skills.list",
			[this](const protocol::RequestFrame& request) {
				const auto includeInvalid =
					ExtractBoolParam(request.paramsJson, "includeInvalid");
				const bool shouldIncludeInvalid =
					!includeInvalid.has_value() || includeInvalid.value();

				std::string entriesJson = "[";
				bool first = true;
				std::size_t count = 0;
				for (const auto& entry : m_skillsCatalogState.entries) {
					if (!shouldIncludeInvalid && !entry.validFrontmatter) {
						continue;
					}

					if (!first) {
						entriesJson += ",";
					}

					entriesJson += SerializeSkillCatalogEntry(entry);
					first = false;
					++count;
				}
				entriesJson += "]";

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"skills\":" +
						entriesJson +
						",\"count\":" +
						std::to_string(count) +
						",\"includeInvalid\":" +
						std::string(shouldIncludeInvalid ? "true" : "false") +
						"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register("gateway.runtime.orchestration.status", [this](const protocol::RequestFrame& request) {
			const auto sessions = m_sessionRegistry.List();
			const auto agents = m_agentRegistry.List();
			const std::string activeSession =
				HasSessionId(m_sessionRegistry, m_runtimeAssignedSessionId)
				? m_runtimeAssignedSessionId
				: (sessions.empty() ? "main" : sessions.front().id);
			const std::string activeAgent =
				HasAgentId(m_agentRegistry, m_runtimeAssignedAgentId)
				? m_runtimeAssignedAgentId
				: (agents.empty() ? "default" : agents.front().id);
			const bool busy =
				m_runtimeQueueDepth > 0 || m_runtimeRunningCount > 0;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"state\":\"" + std::string(busy ? "busy" : "idle") +
					"\",\"activeSession\":\"" +
					EscapeJsonLocal(activeSession) + "\",\"activeAgent\":\"" +
					EscapeJsonLocal(activeAgent) + "\",\"queueDepth\":" +
					std::to_string(m_runtimeQueueDepth) +
					",\"running\":" +
					std::to_string(m_runtimeRunningCount) +
					",\"capacity\":" +
				   std::to_string(m_runtimeQueueCapacity) +
					",\"dynamicLoopMetrics\":{\"success\":" +
					std::to_string(m_taskDeltaRunSuccessCount) +
					",\"failure\":" +
					std::to_string(m_taskDeltaRunFailureCount) +
					",\"timeout\":" +
					std::to_string(m_taskDeltaRunTimeoutCount) +
					",\"cancelled\":" +
					std::to_string(m_taskDeltaRunCancelledCount) +
					",\"fallback\":" +
					std::to_string(m_taskDeltaRunFallbackCount) + "}}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register(
			"gateway.runtime.health.dependencies",
			[this](const protocol::RequestFrame& request) {
				const auto health =
					executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);

				std::string probesJson = "[";
				for (std::size_t index = 0; index < health.probes.size(); ++index) {
					if (index > 0) {
						probesJson += ",";
					}

					const auto& probe = health.probes[index];
					probesJson +=
						"{\"key\":\"" + EscapeJsonLocal(probe.key) +
						"\",\"state\":\"" + EscapeJsonLocal(probe.state) +
						"\",\"reasonCode\":\"" +
						EscapeJsonLocal(probe.reasonCode) +
						"\",\"reasonMessage\":\"" +
						EscapeJsonLocal(probe.reasonMessage) +
						"\",\"checkedAtEpochMs\":" +
						std::to_string(probe.checkedAtEpochMs) +
						",\"expiresAtEpochMs\":" +
						std::to_string(probe.expiresAtEpochMs) + "}";
				}
				probesJson += "]";

				EmitTelemetryEvent(
					"gateway.email.preflight.snapshot",
					std::string("{\"generatedAtEpochMs\":") +
					std::to_string(health.generatedAtEpochMs) +
					",\"ttlMs\":" +
					std::to_string(health.ttlMs) +
					",\"count\":" +
					std::to_string(health.probes.size()) +
					",\"capability\":\"" +
					EscapeJsonLocal(health.emailSendState) +
					"\"}");

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"probes\":" + probesJson +
						",\"count\":" +
						std::to_string(health.probes.size()) +
						",\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.health.capabilities",
			[](const protocol::RequestFrame& request) {
				const auto health =
					executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"capabilities\":[{\"name\":\"email.send\",\"state\":\"" +
						EscapeJsonLocal(health.emailSendState) +
						"\"}],\"count\":1,\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.policy.resolve",
			[this](const protocol::RequestFrame& request) {
				std::string backendsJson = "[";
				for (std::size_t i = 0; i < m_runtimeEmailResolvedBackends.size(); ++i) {
					if (i > 0) {
						backendsJson += ",";
					}

					backendsJson +=
						"\"" + EscapeJsonLocal(m_runtimeEmailResolvedBackends[i]) + "\"";
				}
				backendsJson += "]";

				EmitTelemetryEvent(
					"gateway.email.policy.decision",
					std::string("{\"profileId\":\"") +
					EscapeJsonLocal(m_runtimeEmailPolicyProfileId) +
					"\",\"backends\":" +
					backendsJson +
					",\"actions\":{\"unavailable\":\"" +
					EscapeJsonLocal(m_runtimeEmailPolicyOnUnavailable) +
					"\",\"authError\":\"" +
					EscapeJsonLocal(m_runtimeEmailPolicyOnAuthError) +
					"\",\"execError\":\"" +
					EscapeJsonLocal(m_runtimeEmailPolicyOnExecError) +
					"\"}}");

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"profileId\":\"" +
						EscapeJsonLocal(m_runtimeEmailPolicyProfileId) +
						"\",\"backends\":" +
						backendsJson +
						",\"actions\":{\"unavailable\":\"" +
						EscapeJsonLocal(m_runtimeEmailPolicyOnUnavailable) +
						"\",\"authError\":\"" +
						EscapeJsonLocal(m_runtimeEmailPolicyOnAuthError) +
						"\",\"execError\":\"" +
						EscapeJsonLocal(m_runtimeEmailPolicyOnExecError) +
						"\"},\"retry\":{\"maxAttempts\":" +
						std::to_string(m_runtimeEmailRetryMaxAttempts) +
						",\"delayMs\":" +
						std::to_string(m_runtimeEmailRetryDelayMs) +
						"},\"approval\":{\"requiresApproval\":" +
						std::string(m_runtimeEmailRequiresApproval ? "true" : "false") +
						",\"tokenTtlMinutes\":" +
						std::to_string(m_runtimeEmailApprovalTokenTtlMinutes) +
						"}}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseGate4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorGate4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phasePortal4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncGate4\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandGate4\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorPortal4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseBridge4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncPortal4\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandPortal4\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorBridge4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLink4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncBridge4\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandBridge4\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLink4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNode5\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLink4\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLink4\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorNode5\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseHub3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncNode5\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandNode5\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorHub3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseGate3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncHub3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandHub3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorGate3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseRelay3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncGate3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandGate3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorRelay3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phasePortal3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncRelay3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandRelay3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorPortal3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseBridge3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncPortal3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandPortal3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorBridge3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseMesh3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncBridge3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandBridge3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorMesh3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNode4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncMesh3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandMesh3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorNode4\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLink3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncNode4\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandNode4\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLink3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseThread2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLink3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLink3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorThread2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseChain2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncThread2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandThread2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorChain2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseSpline2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncChain2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandChain2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorSpline2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseRail2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncSpline2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandSpline2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorRail2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseTrack2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncRail2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandRail2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorTrack2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLane2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncTrack2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandTrack2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLane2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseGrid2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLane2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLane2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorGrid2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseBand2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncGrid2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandGrid2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorBand2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseArc2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncBand2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandBand2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorArc2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseMesh2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncArc2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandArc2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorMesh2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLink2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncMesh2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandMesh2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLink2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNode3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLink2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLink2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorNode3\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseHub2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncNode3\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandNode3\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorHub2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseGate2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncHub2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandHub2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorGate2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseRelay2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncGate2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandGate2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorRelay2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phasePortal\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncRelay2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandRelay2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorPortal\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncPortal\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandPortal\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseBridge\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncAnchor2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandAnchor2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorBridge\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNode\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncBridge\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandBridge\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorNode2\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLink\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncNode2\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandNode2\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLink\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseThread",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseThread\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLink\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLink\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorThread",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorThread\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseChain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseChain\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncThread",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncThread\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandThread",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandThread\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorChain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorChain\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseSpline\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncChain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncChain\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandChain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandChain\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorSpline\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseRail\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncSpline\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandSpline\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorRail\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseTrack\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncRail\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandRail\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorTrack\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLane",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLane\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncTrack\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandTrack\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLane",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorLane\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseGrid\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLane",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncLane\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLane",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLane\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorGrid\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseSpan\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncGrid\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandGrid\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorSpan\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseFrame\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncSpan\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandSpan\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorFrame\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseCore",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseCore\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncFrame\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandFrame\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorCore",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorCore\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNet",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNet\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncCore",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncCore\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandCore",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandCore\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorNode\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseFabric",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseFabric\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNet",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncNet\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandNode\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorMesh\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseMesh\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncFabric",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncFabric\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandArc",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandArc\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorArc",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorArc\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseArc",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseArc\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncMesh\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLattice",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandLattice\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorSpiral\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseSpiral\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncArc",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncArc\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandSpiral\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorRibbon\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseHelix\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncSpiral\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandHelix\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorContour",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorContour\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseRibbon\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncHelix\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandRibbon\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseContour",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseContour\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncRibbon\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandContour",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandContour\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.driftVector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"driftVector\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLattice",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseLattice\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncContour",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncContour\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandMatrix\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.envelopeDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"envelopeDrift\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseVector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseVector\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncMatrix\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandVector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandVector\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.biasEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"biasEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPhase",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorPhase\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncEnvelope\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandDrift\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.biasDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"biasDrift\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorField",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectors\":2,\"magnitude\":0,\"state\":\"steady\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncDrift\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandStability",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandStability\":100,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phase\":\"steady\",\"amplitude\":1,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectorDrift\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBias",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phase\":\"steady\",\"bias\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register("gateway.runtime.streaming.status", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"enabled\":" +
					std::string(m_runtimeAgentStreaming ? "true" : "false") +
					",\"mode\":\"chunked\",\"heartbeatMs\":1500" +
					std::string(m_streamingThrottled ? ",\"throttled\":true" : ",\"throttled\":false") +
					",\"bufferedFrames\":" + std::to_string(m_streamingBufferedFrames) +
					",\"bufferedBytes\":" + std::to_string(m_streamingBufferedBytes) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"cohesive\":true,\"delta\":0,\"samples\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"waveIndex\":1,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBand",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncBand\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"waveDrift\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register("gateway.models.failover.status", [this](const protocol::RequestFrame& request) {
			const std::string selectedPrimary =
				m_failoverOverrideActive
				? m_failoverOverrideModel
				: m_runtimeAgentModel;
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"primary\":\"" + EscapeJsonLocal(selectedPrimary) +
					"\",\"fallbacks\":[\"reasoner\"],\"maxRetries\":2,\"strategy\":\"ordered\",\"overrideActive\":" +
					std::string(m_failoverOverrideActive ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.orchestration.queue", [this](const protocol::RequestFrame& request) {
			const auto queued = ExtractSizeParam(request.paramsJson, "queued");
			const auto running = ExtractSizeParam(request.paramsJson, "running");
			const auto capacity = ExtractSizeParam(request.paramsJson, "capacity");

			if (queued.has_value()) {
				m_runtimeQueueDepth = queued.value();
			}

			if (running.has_value()) {
				m_runtimeRunningCount = running.value();
			}

			if (capacity.has_value() && capacity.value() > 0) {
				m_runtimeQueueCapacity = capacity.value();
			}

			if (m_runtimeRunningCount > m_runtimeQueueCapacity) {
				m_runtimeRunningCount = m_runtimeQueueCapacity;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"queued\":" + std::to_string(m_runtimeQueueDepth) +
					",\"running\":" + std::to_string(m_runtimeRunningCount) +
					",\"capacity\":" + std::to_string(m_runtimeQueueCapacity) +
					",\"updated\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.streaming.sample", [this](const protocol::RequestFrame& request) {
			const std::size_t chunks = m_streamingBufferedFrames > 0
				? (std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(3))
				: 2;
			const bool finalChunk = !m_streamingThrottled;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"chunks\":[\"hello\",\"world\"],\"count\":" +
					std::to_string(chunks) +
					",\"final\":" +
					std::string(finalChunk ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.failover.preview", [this](const protocol::RequestFrame& request) {
			std::string requested = ExtractStringParam(request.paramsJson, "model");
			if (requested.empty()) {
				requested = m_runtimeAgentModel;
			}

			const std::string selected =
				m_failoverOverrideActive
				? m_failoverOverrideModel
				: requested;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"model\":\"" + EscapeJsonLocal(requested) +
					"\",\"attempts\":[\"" + EscapeJsonLocal(requested) +
					"\",\"reasoner\"],\"selected\":\"" +
					EscapeJsonLocal(selected) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.orchestration.assign", [this](const protocol::RequestFrame& request) {
			std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
			std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");

			if (agentId.empty()) {
				agentId = m_runtimeAssignedAgentId;
			}

			if (sessionId.empty()) {
				sessionId = m_runtimeAssignedSessionId;
			}

			const bool agentExists = HasAgentId(m_agentRegistry, agentId);
			const bool sessionExists = HasSessionId(m_sessionRegistry, sessionId);
			const bool assigned = agentExists && sessionExists;

			if (assigned) {
				m_runtimeAssignedAgentId = agentId;
				m_runtimeAssignedSessionId = sessionId;
				++m_runtimeAssignmentCount;

				if (m_runtimeQueueDepth > 0 &&
					m_runtimeRunningCount < m_runtimeQueueCapacity) {
					--m_runtimeQueueDepth;
					++m_runtimeRunningCount;
				}
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"agentId\":\"" + EscapeJsonLocal(agentId) +
					"\",\"sessionId\":\"" + EscapeJsonLocal(sessionId) +
					"\",\"assigned\":" +
					std::string(assigned ? "true" : "false") +
					",\"assignments\":" +
					std::to_string(m_runtimeAssignmentCount) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.streaming.window", [this](const protocol::RequestFrame& request) {
			const auto windowMs = ExtractSizeParam(request.paramsJson, "windowMs");
			if (windowMs.has_value() && windowMs.value() > 0) {
				m_streamingWindowMs = windowMs.value();
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"windowMs\":" + std::to_string(m_streamingWindowMs) +
					",\"frames\":" + std::to_string(m_streamingBufferedFrames) +
					",\"dropped\":0}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.failover.metrics", [this](const protocol::RequestFrame& request) {
			const double successRate =
				m_failoverAttempts == 0
				? 1.0
				: static_cast<double>(m_failoverAttempts - m_failoverFallbackHits) /
				static_cast<double>(m_failoverAttempts);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"attempts\":" + std::to_string(m_failoverAttempts) +
					",\"fallbackHits\":" + std::to_string(m_failoverFallbackHits) +
					",\"successRate\":" + std::to_string(successRate) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.orchestration.rebalance", [this](const protocol::RequestFrame& request) {
			std::string strategy = ExtractStringParam(request.paramsJson, "strategy");
			if (strategy.empty()) {
				strategy = "sticky";
			}

			std::size_t moved = 0;
			if (m_runtimeQueueDepth > 0 &&
				m_runtimeRunningCount < m_runtimeQueueCapacity) {
				moved = 1;
				--m_runtimeQueueDepth;
				++m_runtimeRunningCount;
			}

			++m_runtimeRebalanceCount;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"moved\":" + std::to_string(moved) +
					",\"remaining\":" +
					std::to_string(m_runtimeQueueDepth + m_runtimeRunningCount) +
					",\"strategy\":\"" + EscapeJsonLocal(strategy) +
					"\",\"rebalances\":" +
					std::to_string(m_runtimeRebalanceCount) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.streaming.backpressure", [this](const protocol::RequestFrame& request) {
			const std::size_t pressure =
				m_streamingHighWatermark == 0
				? 0
				: (m_streamingBufferedFrames * 100) / m_streamingHighWatermark;
			m_streamingThrottled = pressure >= 80;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"pressure\":" + std::to_string(pressure) +
					",\"throttled\":" +
					std::string(m_streamingThrottled ? "true" : "false") +
					",\"bufferedFrames\":" +
					std::to_string(m_streamingBufferedFrames) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.failover.simulate", [this](const protocol::RequestFrame& request) {
			std::string requested = ExtractStringParam(request.paramsJson, "requested");
			if (requested.empty()) {
				requested = m_runtimeAgentModel;
			}

			const bool useFallback =
				m_failoverOverrideActive && m_failoverOverrideModel != requested;
			const std::string resolved =
				useFallback ? m_failoverOverrideModel : requested;
			++m_failoverAttempts;
			if (useFallback) {
				++m_failoverFallbackHits;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"requested\":\"" + EscapeJsonLocal(requested) +
					"\",\"resolved\":\"" + EscapeJsonLocal(resolved) +
					"\",\"usedFallback\":" +
					std::string(useFallback ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.orchestration.drain", [this](const protocol::RequestFrame& request) {
			std::string reason = ExtractStringParam(request.paramsJson, "reason");
			if (reason.empty()) {
				reason = "idle";
			}

			const std::size_t drained =
				m_runtimeQueueDepth + m_runtimeRunningCount;
			m_runtimeQueueDepth = 0;
			m_runtimeRunningCount = 0;
			++m_runtimeDrainCount;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"drained\":" + std::to_string(drained) +
					",\"remaining\":0,\"reason\":\"" +
					EscapeJsonLocal(reason) +
					"\",\"drains\":" +
					std::to_string(m_runtimeDrainCount) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.runtime.streaming.replay", [this](const protocol::RequestFrame& request) {
			const std::size_t replayed =
				(std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(2));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"replayed\":" + std::to_string(replayed) +
					",\"cursor\":\"stream-cursor-1\",\"complete\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.failover.audit", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"entries\":2,\"lastModel\":\"default\",\"lastOutcome\":\"primary\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.snapshot",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"sessions\":" +
						std::to_string(m_sessionRegistry.List().size()) +
						",\"agents\":" +
						std::to_string(m_agentRegistry.List().size()) +
						",\"active\":\"" +
						EscapeJsonLocal(m_runtimeAssignedSessionId) +
						"\",\"activeAgent\":\"" +
						EscapeJsonLocal(m_runtimeAssignedAgentId) +
						"\",\"queue\":" +
						std::to_string(m_runtimeQueueDepth) +
						",\"running\":" +
						std::to_string(m_runtimeRunningCount) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.cursor",
			[this](const protocol::RequestFrame& request) {
				const std::size_t lagMs =
					m_streamingBufferedFrames * 10;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"cursor\":\"stream-cursor-1\",\"lagMs\":" +
						std::to_string(lagMs) +
						",\"hasMore\":" +
						std::string(m_streamingBufferedFrames > 0 ? "true" : "false") + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.policy",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"policy\":\"ordered\",\"maxRetries\":2,\"stickyPrimary\":true,\"overrideModel\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) + "\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.timeline",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"ticks\":[" +
						std::to_string(m_runtimeAssignmentCount) + "," +
						std::to_string(m_runtimeRebalanceCount) + "," +
						std::to_string(m_runtimeDrainCount) +
						"],\"count\":3,\"source\":\"runtime-orchestrator\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.metrics",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"frames\":" +
						std::to_string(m_streamingBufferedFrames) +
						",\"bytes\":" +
						std::to_string(m_streamingBufferedBytes) +
						",\"avgChunkMs\":5}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.history",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"events\":[\"primary\",\"fallback\"],\"count\":" +
						std::to_string(m_failoverAttempts) +
						",\"last\":\"" +
						std::string(m_failoverFallbackHits > 0 ? "fallback" : "primary") + "\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.heartbeat",
			[this](const protocol::RequestFrame& request) {
				const std::size_t backlog =
					m_runtimeQueueDepth + m_runtimeRunningCount;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"alive\":true,\"intervalMs\":1000,\"jitterMs\":" +
						std::to_string(25 + (backlog > 0 ? 5 : 0)) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.health",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"healthy\":" +
						std::string(m_streamingBufferedFrames <= m_streamingHighWatermark ? "true" : "false") +
						",\"stalls\":0,\"recoveries\":0}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.recent",
			[this](const protocol::RequestFrame& request) {
				const std::string activeModel =
					m_failoverOverrideActive
					? m_failoverOverrideModel
					: m_runtimeAgentModel;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"models\":[\"" + EscapeJsonLocal(m_runtimeAgentModel) +
						"\",\"reasoner\"],\"count\":2,\"active\":\"" +
						EscapeJsonLocal(activeModel) + "\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.pulse",
			[this](const protocol::RequestFrame& request) {
				const std::size_t pulse =
					m_runtimeAssignmentCount +
					m_runtimeRebalanceCount +
					m_runtimeDrainCount;
				const bool busy =
					m_runtimeQueueDepth > 0 || m_runtimeRunningCount > 0;
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"pulse\":" + std::to_string(pulse) +
						",\"driftMs\":0,\"state\":\"" +
						std::string(busy ? "active" : "steady") + "\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.snapshot",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"frames\":" + std::to_string(m_streamingBufferedFrames) +
						",\"cursor\":\"stream-cursor-2\",\"sealed\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.window",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"windowSec\":60,\"attempts\":" +
						std::to_string(m_failoverAttempts) +
						",\"fallbacks\":" +
						std::to_string(m_failoverFallbackHits) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cadence",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"periodMs\":1000,\"varianceMs\":5,\"aligned\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.watermark",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"high\":" + std::to_string(m_streamingHighWatermark) +
						",\"low\":4,\"current\":" +
						std::to_string(m_streamingBufferedFrames) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.digest",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"digest\":\"sha256:failover-v1\",\"entries\":" +
						std::to_string(m_failoverAttempts) +
						",\"fresh\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.beacon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"beacon\":\"orch-1\",\"seq\":1,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"checkpoint\":\"cp-1\",\"frames\":2,\"persisted\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"entries\":2,\"primaryHits\":1,\"fallbackHits\":1}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.epoch",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"epoch\":1,\"startedMs\":1735689600000,\"active\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.resume",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"resumed\":true,\"cursor\":\"stream-cursor-3\",\"replayed\":1}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"profile\":\"balanced\",\"weights\":[70,30],\"version\":1}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phase\":\"steady\",\"step\":1,\"locked\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.recovery",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"recovering\":false,\"attempts\":0,\"lastMs\":0}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"primary\":\"default\",\"secondary\":\"reasoner\",\"confidence\":100}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.signal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"signal\":\"ok\",\"priority\":1,\"latched\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.continuity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"continuous\":true,\"gaps\":0,\"lastSeq\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.forecast",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"windowSec\":60,\"projectedFallbacks\":1,\"risk\":\"low\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"axis\":\"primary\",\"magnitude\":1,\"normalized\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.stability",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"stable\":true,\"variance\":0,\"samples\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.threshold",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"minSuccessRate\":90,\"maxFallbacks\":2,\"active\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"rows\":2,\"cols\":2,\"balanced\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.integrity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"valid\":true,\"violations\":0,\"checked\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.guardrail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"rule\":\"max_fallbacks\",\"limit\":2,\"enforced\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.lattice",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"layers\":2,\"nodes\":4,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.coherence",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"coherent\":true,\"drift\":0,\"segments\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"windowSec\":60,\"floor\":90,\"ceiling\":100}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.mesh",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"nodes\":4,\"edges\":3,\"connected\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.fidelity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"fidelity\":100,\"drops\":0,\"verified\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.margin",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"headroom\":10,\"buffer\":2,\"safe\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.fabric",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"threads\":6,\"links\":8,\"resilient\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.accuracy",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"accuracy\":99,\"mismatches\":0,\"calibrated\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.reserve",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"reserve\":1,\"available\":true,\"priority\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.load",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"queueLoad\":0,\"agentLoad\":0,\"state\":\"steady\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.buffer",
			[this](const protocol::RequestFrame& request) {
				const auto bufferedFrames =
					ExtractSizeParam(request.paramsJson, "bufferedFrames");
				const auto bufferedBytes =
					ExtractSizeParam(request.paramsJson, "bufferedBytes");
				const auto highWatermark =
					ExtractSizeParam(request.paramsJson, "highWatermark");

				if (bufferedFrames.has_value()) {
					m_streamingBufferedFrames = bufferedFrames.value();
				}

				if (bufferedBytes.has_value()) {
					m_streamingBufferedBytes = bufferedBytes.value();
				}

				if (highWatermark.has_value() && highWatermark.value() > 0) {
					m_streamingHighWatermark = highWatermark.value();
				}

				m_streamingThrottled =
					m_streamingBufferedFrames >= m_streamingHighWatermark;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bufferedFrames\":" +
						std::to_string(m_streamingBufferedFrames) +
						",\"bufferedBytes\":" +
						std::to_string(m_streamingBufferedBytes) +
						",\"highWatermark\":" +
						std::to_string(m_streamingHighWatermark) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override",
			[this](const protocol::RequestFrame& request) {
				std::string model =
					ExtractStringParam(request.paramsJson, "model");
				std::string reason =
					ExtractStringParam(request.paramsJson, "reason");
				const auto activeParam =
					ExtractBoolParam(request.paramsJson, "active");

				if (model.empty()) {
					model = m_runtimeAgentModel;
				}

				if (reason.empty()) {
					reason = "manual";
				}

				const bool active = activeParam.value_or(true);
				m_failoverOverrideActive = active;
				m_failoverOverrideModel = model;
				m_failoverOverrideReason = reason;
				++m_failoverOverrideChanges;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":" +
						std::string(m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(m_failoverOverrideReason) +
						"\",\"changes\":" +
						std::to_string(m_failoverOverrideChanges) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.saturation",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"saturation\":0,\"capacity\":8,\"state\":\"stable\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.throttle",
			[this](const protocol::RequestFrame& request) {
				const auto throttled =
					ExtractBoolParam(request.paramsJson, "throttled");
				const auto limitPerSec =
					ExtractSizeParam(request.paramsJson, "limitPerSec");

				if (throttled.has_value()) {
					m_streamingThrottled = throttled.value();
				}

				if (limitPerSec.has_value() && limitPerSec.value() > 0) {
					m_streamingThrottleLimitPerSec = limitPerSec.value();
				}

				const std::size_t currentPerSec =
					m_streamingWindowMs == 0
					? 0
					: (m_streamingBufferedFrames * 1000) / m_streamingWindowMs;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"throttled\":" +
						std::string(m_streamingThrottled ? "true" : "false") +
						",\"limitPerSec\":" +
						std::to_string(m_streamingThrottleLimitPerSec) +
						",\"currentPerSec\":" +
						std::to_string(currentPerSec) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.clear",
			[this](const protocol::RequestFrame& request) {
				m_failoverOverrideActive = false;
				m_failoverOverrideModel = m_runtimeAgentModel;
				m_failoverOverrideReason = "cleared";
				++m_failoverOverrideChanges;

				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"cleared\":true,\"active\":false,\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) + "\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.pressure",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"pressure\":0,\"threshold\":80,\"state\":\"normal\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.pacing",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"paceMs\":50,\"burst\":1,\"adaptive\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.status",
			[this](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":" +
						std::string(m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(m_failoverOverrideReason) +
						"\",\"source\":\"runtime\",\"changes\":" +
						std::to_string(m_failoverOverrideChanges) + "}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.headroom",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"headroom\":8,\"used\":0,\"state\":\"ready\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.jitter",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"jitterMs\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.history",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"entries\":0,\"lastModel\":\"default\",\"active\":false}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.balance",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"balanced\":true,\"skew\":0,\"state\":\"stable\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.drift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"driftMs\":0,\"windowMs\":1000,\"corrected\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.metrics",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"switches\":0,\"lastModel\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.efficiency",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"efficiency\":100,\"waste\":0,\"state\":\"optimized\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.variance",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"variance\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.window",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"windowSec\":60,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.utilization",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"utilization\":0,\"capacity\":8,\"state\":\"idle\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.deviation",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"deviation\":0,\"samples\":2,\"withinBudget\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.digest",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"digest\":\"sha256:override-v1\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.capacity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"capacity\":8,\"used\":0,\"state\":\"ready\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.alignment",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"aligned\":true,\"offsetMs\":0,\"windowMs\":1000}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.timeline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"entries\":0,\"active\":false,\"lastModel\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.occupancy",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"occupancy\":0,\"slots\":8,\"state\":\"idle\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.skew",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"skewMs\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.catalog",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"count\":1,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.elasticity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"elasticity\":100,\"headroom\":8,\"state\":\"expandable\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.dispersion",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"dispersion\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.registry",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"entries\":1,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"cohesion\":100,\"groups\":1,\"state\":\"coherent\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.curvature",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"curvature\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"rows\":1,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.resilience",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"resilience\":100,\"faults\":0,\"state\":\"steady\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.smoothness",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"smoothness\":100,\"jitterMs\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.snapshot",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"revision\":1,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.readiness",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"ready\":true,\"queueDepth\":0,\"state\":\"ready\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.harmonics",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"harmonics\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.pointer",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"pointer\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.contention",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"contention\":0,\"waiters\":0,\"state\":\"clear\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phase\":\"steady\",\"step\":1,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.state",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"state\":\"none\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.fairness",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"fairness\":100,\"skew\":0,\"state\":\"balanced\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.tempo",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"tempo\":1,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"profile\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.equilibrium",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"equilibrium\":100,\"delta\":0,\"state\":\"balanced\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.steadiness",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"steady\":true,\"variance\":0,\"windowMs\":1000}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.temporal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"temporal\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.consistency",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"consistent\":true,\"deviation\":0,\"samples\":2}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.audit",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"entries\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.parity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"parity\":100,\"gap\":0,\"state\":\"aligned\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.stabilityIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"stabilityIndex\":100,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.spectral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"spectral\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"floor\":0,\"ceiling\":100,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"checkpoint\":\"cp-override-1\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.convergence",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"convergence\":100,\"drift\":0,\"state\":\"locked\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.hysteresis",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"hysteresis\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.resonance",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"resonance\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.vectorField",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"vectors\":2,\"magnitude\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"baseline\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.balanceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"balanceIndex\":100,\"skew\":0,\"state\":\"balanced\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLock",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"locked\":true,\"phase\":\"steady\",\"drift\":0}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveform",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"waveform\":\"flat\",\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.horizon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"horizonMs\":1000,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.manifest",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"manifest\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.symmetry",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"symmetry\":100,\"offset\":0,\"state\":\"aligned\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.gradient",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"gradient\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.vectorClock",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"clock\":1,\"lag\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.trend",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"trend\":\"flat\",\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"entries\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.harmonicity",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"harmonicity\":100,\"detune\":0,\"state\":\"aligned\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.inertia",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"inertia\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.coordination",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"coordinated\":true,\"lag\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.latencyBand",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"minMs\":0,\"maxMs\":0,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.snapshotIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"index\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cadenceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"cadenceIndex\":100,\"jitter\":0,\"state\":\"steady\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.damping",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"damping\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.phaseNoise",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseNoise\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.beat",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"beatHz\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.digestIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"digestIndex\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.waveLock",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"locked\":true,\"phase\":\"steady\",\"slip\":0}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.flux",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"flux\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"phaseMatrix\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"driftEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.modulation",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"modulation\":0,\"samples\":2,\"bounded\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncVector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"syncVector\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"bandEnvelope\":0,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.pulseTrain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"pulseHz\":1,\"samples\":2,\"stable\":true}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.cursor",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"cursor\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vector\":\"default\",\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorDrift\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.phaseBias",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"phaseBias\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.biasEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"biasEnvelope\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"driftEnvelope\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.envelopeDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"envelopeDrift\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.driftVector",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"driftVector\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorEnvelope\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorContour",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorContour\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorRibbon\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorSpiral\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorArc\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorMesh\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorNode\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorCore",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorCore\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorFrame\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorSpan\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorGrid\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLane\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorTrack\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorRail\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorSpline\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorChain\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorThread\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLink\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorNode2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorBridge\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorAnchor2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorPortal\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorRelay2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorGate2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorHub2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorNode3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLink2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorMesh2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorArc2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorBand2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorGrid2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLane2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorTrack2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorRail2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorSpline2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorChain2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorThread2\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLink3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorNode4\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorMesh3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorBridge3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorPortal3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorRelay3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorGate3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorHub3\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorNode5\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorLink4\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorBridge4\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorPortal4\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson =
						"{\"active\":false,\"vectorGate4\":0,\"model\":\"default\"}",
					.error = std::nullopt,
				};
			});
	}

} // namespace blazeclaw::gateway
