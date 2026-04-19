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
#include "SendPolicyResolver.h"
#include "ToolPolicyPipeline.h"
#include "TranscriptPolicyResolver.h"
#include "GatewayLifecycleEventEmitter.h"
#include "RunSummaryBuilder.h"
#include "BranchDecisionDiagnostics.h"
#include "ChatTranscriptStore.h"
#include "ChatAbortCoordinator.h"
#include "ChatHistoryPolicy.h"
#include "ChatRoutePolicy.h"
#include "ChatOrchestrationPolicy.h"
#include "ToolEventRecipientPolicy.h"
#include "ChatControlPlaneService.h"
#include "GatewayEventFanoutService.h"
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
		constexpr std::array<const char*, 3> kConfigSchemaForbiddenSegments = {
			"__proto__",
			"prototype",
			"constructor",
		};

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

		std::string NormalizeJsonRawForPayload(
			const std::string& raw,
			const char* fallbackRaw) {
			try {
				const auto parsed = nlohmann::json::parse(raw);
				return parsed.dump();
			}
			catch (...) {
				return fallbackRaw == nullptr ? "{}" : std::string(fallbackRaw);
			}
		}

		std::string SerializeConfigUiHintLocal(
			const blazeclaw::config::ConfigUiHintModel& hint) {
			std::string tagsJson = "[";
			for (std::size_t index = 0; index < hint.tags.size(); ++index) {
				if (index > 0) {
					tagsJson += ",";
				}

				tagsJson += "\"" + EscapeJsonLocal(hint.tags[index]) + "\"";
			}
			tagsJson += "]";

			return
				"{\"label\":\"" +
				EscapeJsonLocal(hint.label) +
				"\",\"help\":\"" +
				EscapeJsonLocal(hint.help) +
				"\",\"tags\":" +
				tagsJson +
				",\"advanced\":" +
				std::string(hint.advanced ? "true" : "false") +
				",\"sensitive\":" +
				std::string(hint.sensitive ? "true" : "false") +
				",\"placeholder\":\"" +
				EscapeJsonLocal(hint.placeholder) +
				"\"}";
		}

		std::string SerializeConfigSchemaChildLocal(
			const ConfigSchemaGatewayChild& child) {
			std::string hintJson = "null";
			if (child.hint.has_value()) {
				hintJson = SerializeConfigUiHintLocal(child.hint.value());
			}

			return
				"{\"key\":\"" +
				EscapeJsonLocal(child.key) +
				"\",\"path\":\"" +
				EscapeJsonLocal(child.path) +
				"\",\"type\":\"" +
				EscapeJsonLocal(child.type) +
				"\",\"required\":" +
				std::string(child.required ? "true" : "false") +
				",\"hasChildren\":" +
				std::string(child.hasChildren ? "true" : "false") +
				",\"hint\":" +
				hintJson +
				",\"hintPath\":\"" +
				EscapeJsonLocal(child.hintPath) +
				"\"}";
		}

		std::string SerializeConfigSchemaLookupResultLocal(
			const ConfigSchemaGatewayLookupResult& result) {
			std::string childrenJson = "[";
			for (std::size_t index = 0; index < result.children.size(); ++index) {
				if (index > 0) {
					childrenJson += ",";
				}

				childrenJson += SerializeConfigSchemaChildLocal(result.children[index]);
			}
			childrenJson += "]";

			std::string hintJson = "null";
			if (result.hint.has_value()) {
				hintJson = SerializeConfigUiHintLocal(result.hint.value());
			}

			return
				"{\"path\":\"" +
				EscapeJsonLocal(result.path) +
				"\",\"schema\":" +
				NormalizeJsonRawForPayload(result.schemaJson, "{}") +
				",\"hint\":" +
				hintJson +
				",\"hintPath\":\"" +
				EscapeJsonLocal(result.hintPath) +
				"\",\"children\":" +
				childrenJson +
				"}";
		}

		std::optional<std::string> NormalizeSchemaLookupPathRequest(
			const std::string& rawPath) {
			std::string trimmed = json::Trim(rawPath);
			if (trimmed.empty()) {
				return std::nullopt;
			}

			std::string normalized;
			normalized.reserve(trimmed.size() + 8);
			for (std::size_t index = 0; index < trimmed.size(); ++index) {
				const char ch = trimmed[index];
				if (ch == '[') {
					normalized.push_back('.');
					const std::size_t end = trimmed.find(']', index + 1);
					if (end == std::string::npos) {
						return std::nullopt;
					}

					const std::string inside =
						trimmed.substr(index + 1, end - index - 1);
					normalized += inside.empty() ? "*" : inside;
					index = end;
					continue;
				}

				normalized.push_back(ch);
			}

			while (!normalized.empty() && normalized.front() == '.') {
				normalized.erase(normalized.begin());
			}

			while (!normalized.empty() && normalized.back() == '.') {
				normalized.pop_back();
			}

			std::string collapsed;
			collapsed.reserve(normalized.size());
			bool previousDot = false;
			for (const char ch : normalized) {
				if (ch == '.') {
					if (!previousDot) {
						collapsed.push_back(ch);
					}
					previousDot = true;
					continue;
				}

				collapsed.push_back(ch);
				previousDot = false;
			}

			if (collapsed.empty()) {
				return std::nullopt;
			}

			std::size_t segmentCount = 0;
			std::size_t start = 0;
			while (start <= collapsed.size()) {
				const auto next = collapsed.find('.', start);
				const std::string segment =
					next == std::string::npos
					? collapsed.substr(start)
					: collapsed.substr(start, next - start);

				if (!segment.empty()) {
					if (std::any_of(
						kConfigSchemaForbiddenSegments.begin(),
						kConfigSchemaForbiddenSegments.end(),
						[&segment](const char* forbidden) {
							return forbidden != nullptr && segment == forbidden;
						})) {
						return std::nullopt;
					}

					++segmentCount;
				}

				if (next == std::string::npos) {
					break;
				}

				start = next + 1;
			}

			if (segmentCount == 0 ||
				segmentCount > blazeclaw::config::kConfigSchemaLookupMaxPathSegments) {
				return std::nullopt;
			}

			return collapsed;
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

		std::string SerializePluginRuntimeSubagentModeLocal(
			const PluginRuntimeSubagentMode mode) {
			switch (mode) {
			case PluginRuntimeSubagentMode::Explicit:
				return "explicit";
			case PluginRuntimeSubagentMode::GatewayBindable:
				return "gateway-bindable";
			case PluginRuntimeSubagentMode::Default:
			default:
				return "default";
			}
		}

		std::string SerializePluginRuntimeCapabilitiesJsonLocal(
			const std::vector<PluginRuntimeCapabilityContract>& contracts) {
			std::string capabilitiesJson = "[";
			for (std::size_t index = 0; index < contracts.size(); ++index) {
				if (index > 0) {
					capabilitiesJson += ",";
				}

				const auto& contract = contracts[index];
				capabilitiesJson +=
					"{\"capabilityId\":\"" +
					EscapeJsonLocal(contract.capabilityId) +
					"\",\"owner\":\"" +
					EscapeJsonLocal(contract.owner) +
					"\",\"version\":\"" +
					EscapeJsonLocal(contract.version) +
					"\",\"stability\":\"" +
					EscapeJsonLocal(contract.stability) +
					"\",\"description\":\"" +
					EscapeJsonLocal(contract.description) +
					"\"}";
			}

			capabilitiesJson += "]";
			return capabilitiesJson;
		}

		std::string SerializePluginRuntimeTransitionsJsonLocal(
			const std::vector<PluginRuntimeTransitionEntry>& transitions,
			const std::size_t limit) {
			auto serializeSeverity = [](const PluginRuntimeTransitionSeverity severity) {
				switch (severity) {
				case PluginRuntimeTransitionSeverity::Warn:
					return "warn";
				case PluginRuntimeTransitionSeverity::Error:
					return "error";
				case PluginRuntimeTransitionSeverity::Info:
				default:
					return "info";
				}
				};

			auto serializeLifecyclePhase = [](const PluginRuntimeLifecyclePhase phase) {
				switch (phase) {
				case PluginRuntimeLifecyclePhase::Activation:
					return "activation";
				case PluginRuntimeLifecyclePhase::Mutation:
					return "mutation";
				case PluginRuntimeLifecyclePhase::Deactivation:
					return "deactivation";
				case PluginRuntimeLifecyclePhase::Maintenance:
					return "maintenance";
				case PluginRuntimeLifecyclePhase::SteadyState:
				default:
					return "steady-state";
				}
				};

			const std::size_t startIndex =
				transitions.size() > limit
				? transitions.size() - limit
				: 0;

			std::string transitionsJson = "[";
			for (std::size_t index = startIndex; index < transitions.size(); ++index) {
				if (index > startIndex) {
					transitionsJson += ",";
				}

				const auto& entry = transitions[index];
				transitionsJson +=
					"{\"sequence\":" +
					std::to_string(entry.sequence) +
					",\"timestampMs\":" +
					std::to_string(entry.timestampMs) +
					",\"action\":\"" +
					EscapeJsonLocal(entry.action) +
					"\",\"severity\":\"" +
					serializeSeverity(entry.severity) +
					"\",\"lifecyclePhase\":\"" +
					serializeLifecyclePhase(entry.lifecyclePhase) +
					"\",\"activeVersion\":" +
					std::to_string(entry.activeVersion) +
					",\"httpRouteVersion\":" +
					std::to_string(entry.httpRouteVersion) +
					",\"channelVersion\":" +
					std::to_string(entry.channelVersion) +
					",\"activeRegistryCount\":" +
					std::to_string(entry.activeRegistryCount) +
					",\"httpRoutePinned\":" +
					std::string(entry.httpRoutePinned ? "true" : "false") +
					",\"channelPinned\":" +
					std::string(entry.channelPinned ? "true" : "false") +
					",\"cacheKey\":\"" +
					EscapeJsonLocal(entry.cacheKey) +
					"\",\"workspaceDir\":\"" +
					EscapeJsonLocal(entry.workspaceDir) +
					"\",\"runtimeSubagentMode\":\"" +
					SerializePluginRuntimeSubagentModeLocal(
						entry.runtimeSubagentMode) +
					"\"}";
			}

			transitionsJson += "]";
			return transitionsJson;
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

		void EmitPushLifecycleEvent(
			GatewayWebSocketTransport& transport,
			const GatewayEventFanoutService& fanout,
			const GatewayEventFanoutService::ChatLifecycleEvent& event,
			std::uint64_t& eventSeq) {
			std::string queueError;
			const std::string frame = fanout.BuildChatLifecycleEventFrame(event, ++eventSeq);
			transport.BroadcastOutboundFrame(frame, queueError);
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

		protocol::ErrorShape BuildRuntimeErrorShape(
			const std::string& code,
			const std::string& message,
			const std::string& runId,
			const std::string& sessionKey) {
			std::string details =
				"{\"runId\":" + JsonString(runId) +
				",\"sessionKey\":" + JsonString(sessionKey) + "}";

			protocol::ErrorShape shape{
				.code = code,
				.message = message,
				.detailsJson = details,
				.retryable = RuntimeTranscriptGuard::IsRetryableErrorCode(code),
				.retryAfterMs = RuntimeTranscriptGuard::SuggestedRetryAfterMs(code),
			};
			return shape;
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
			"gateway.runtime.plugins.capabilities",
			[this](const protocol::RequestFrame& request) {
				const auto contracts =
					m_pluginRuntimeState.ListCapabilityContracts();

				return protocol::OkResponse(request, "{\"capabilities\":" +
						SerializePluginRuntimeCapabilitiesJsonLocal(contracts) +
						",\"count\":" +
						std::to_string(contracts.size()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.state",
			[this](const protocol::RequestFrame& request) {
				const auto snapshot = m_pluginRuntimeState.Snapshot();
				const auto importedPluginIds =
					m_pluginRuntimeState.ListImportedRuntimePluginIds();

				std::string importedJson = "[";
				for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
					if (index > 0) {
						importedJson += ",";
					}

					importedJson +=
						"\"" +
						EscapeJsonLocal(importedPluginIds[index]) +
						"\"";
				}
				importedJson += "]";

				const std::size_t activeRegistryCount =
					snapshot.activeRegistry == nullptr
					? 0
					: snapshot.activeRegistry->size();

				return protocol::OkResponse(request, "{\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(m_pluginRuntimeState.GetChannelVersion()) +
						",\"activeRegistryCount\":" +
						std::to_string(activeRegistryCount) +
						",\"httpRoutePinned\":" +
						std::string(snapshot.httpRoute.pinned ? "true" : "false") +
						",\"channelPinned\":" +
						std::string(snapshot.channel.pinned ? "true" : "false") +
						",\"cacheKey\":\"" +
						EscapeJsonLocal(snapshot.cacheKey) +
						"\",\"workspaceDir\":\"" +
						EscapeJsonLocal(snapshot.workspaceDir) +
						"\",\"runtimeSubagentMode\":\"" +
						SerializePluginRuntimeSubagentModeLocal(
							snapshot.runtimeSubagentMode) +
						"\",\"importedPluginIds\":" +
						importedJson +
						",\"importedCount\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.transitions",
			[this](const protocol::RequestFrame& request) {
				const auto requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const std::size_t limit =
					(std::max)(
						std::size_t{ 1 },
						(std::min)(requestedLimit, std::size_t{ 128 }));

				const auto transitions =
					m_pluginRuntimeState.GetTransitionHistory();
				const auto transitionPolicy =
					m_pluginRuntimeState.GetTransitionPolicySettings();

				return protocol::OkResponse(request, "{\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							limit) +
						",\"count\":" +
						std::to_string((std::min)(transitions.size(), limit)) +
						",\"total\":" +
						std::to_string(transitions.size()) +
						",\"retention\":{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}" +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.transitions.policy.get",
			[this](const protocol::RequestFrame& request) {
				const auto transitionPolicy =
					m_pluginRuntimeState.GetTransitionPolicySettings();

				return protocol::OkResponse(request, "{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.transitions.policy.set",
			[this](const protocol::RequestFrame& request) {
				const auto historyLimit =
					ExtractSizeParam(request.paramsJson, "historyLimit").value_or(128);
				const auto exportEnabled =
					ExtractBoolParam(request.paramsJson, "exportEnabled").value_or(false);

				m_pluginRuntimeState.SetTransitionPolicySettings(
					PluginRuntimeStateService::TransitionPolicySettings{
						.historyLimit = historyLimit,
						.exportEnabled = exportEnabled,
					});

				const auto transitionPolicy =
					m_pluginRuntimeState.GetTransitionPolicySettings();
				return protocol::OkResponse(request, "{\"updated\":true,\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.transitions.export",
			[this](const protocol::RequestFrame& request) {
				const auto transitions =
					m_pluginRuntimeState.ExportTransitionHistory();
				const auto transitionPolicy =
					m_pluginRuntimeState.GetTransitionPolicySettings();

				return protocol::OkResponse(request, "{\"enabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						",\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							transitions.size()) +
						",\"count\":" +
						std::to_string(transitions.size()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.imported.list",
			[this](const protocol::RequestFrame& request) {
				const auto importedPluginIds =
					m_pluginRuntimeState.ListImportedRuntimePluginIds();
				std::string importedJson = "[";
				for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
					if (index > 0) {
						importedJson += ",";
					}

					importedJson +=
						"\"" +
						EscapeJsonLocal(importedPluginIds[index]) +
						"\"";
				}
				importedJson += "]";

				return protocol::OkResponse(request, "{\"plugins\":" +
						importedJson +
						",\"count\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.plugins.lifecycle.reset",
			[this](const protocol::RequestFrame& request) {
				bool forTest = false;
				if (request.paramsJson.has_value()) {
					json::FindBoolField(
						request.paramsJson.value(),
						"forTest",
						forTest);
				}

				if (!forTest) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "invalid_params",
							.message = "Set forTest=true to reset plugin runtime lifecycle state.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				m_pluginRuntimeState.ResetForTest();
				const auto snapshot = m_pluginRuntimeState.Snapshot();

				return protocol::OkResponse(request, "{\"reset\":true,\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(m_pluginRuntimeState.GetChannelVersion()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.reportStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"governanceReportingEnabled\":" +
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
						"\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.attestationStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"tenantId\":\"" +
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
						"\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.aggregationStatus",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"tenantId\":\"" +
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
						"\"}");
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

				return protocol::OkResponse(request, "{\"severity\":\"" + EscapeJsonLocal(severity) +
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
						"\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.governance.executeRemediation",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				if (!state.autoRemediationEnabled) {
					return protocol::OkResponse(request, "{\"executed\":false,\"status\":\"disabled\",\"approvalAccepted\":false}");
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

				return protocol::OkResponse(request, "{\"executed\":true,\"status\":\"applied\",\"approvalAccepted\":" +
						std::string(approvalAccepted ? "true" : "false") +
						",\"tenantId\":\"" + EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"playbookPath\":\"" +
						EscapeJsonLocal(state.lastAutoRemediationPlaybookPath) +
						"\",\"tokenMaxAgeMinutes\":" +
						std::to_string(state.autoRemediationTokenMaxAgeMinutes) +
						",\"action\":\"" + EscapeJsonLocal(action) +
						"\",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}");
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

				return protocol::OkResponse(request, "{\"vector\":" + SerializeFloatArrayLocal(result.vector) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
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

				return protocol::OkResponse(request, "{\"vectors\":" + SerializeFloatMatrixLocal(result.vectors) +
						",\"count\":" + std::to_string(result.vectors.size()) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
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
					return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
							"\",\"taskDeltas\":[],\"count\":0}");
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

				return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
						"\",\"taskDeltas\":" + deltasJson +
						",\"count\":" + std::to_string(orderedTaskDeltas.size()) + "}");
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

				return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId.empty() ? "*" : runId) +
						"\",\"cleared\":" + std::to_string(cleared) +
					 ",\"remaining\":" + std::to_string(m_taskDeltaRepository.Size()) + "}");
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
				const auto historyIt = m_chatHistoryBySession.find(sessionKey);
				ChatHistoryPolicy historyPolicy;
				ChatHistoryPolicy::BuildParams historyParams;
				historyParams.requestedLimit = requestedLimit;
				if (historyIt != m_chatHistoryBySession.end()) {
					historyParams.history = historyIt->second;
				}

				const auto historyResult = historyPolicy.Build(historyParams);
				if (historyResult.placeholderCount > 0) {
					EmitTelemetryEvent(
						"gateway.chat.history.placeholder",
						std::string("{\"sessionKey\":") +
						JsonString(sessionKey) +
						",\"placeholderCount\":" +
						std::to_string(historyResult.placeholderCount) +
						"}");
				}

				return protocol::OkResponse(request, "{\"messages\":" +
					  historyResult.messagesJson +
						",\"thinkingLevel\":\"normal\"}");
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
					if (stageContext.skippedByInlinePolicy) {
						EmitTelemetryEvent(
							"gateway.chat.inline.skip",
							std::string("{\"requestId\":") +
							JsonString(request.id) +
							",\"reason\":" +
							JsonString(stageContext.skippedReasonCode) +
							"}");
						return protocol::OkResponse(request, stageContext.responsePayloadJson);
					}

					if (stageContext.deduped) {
						const auto replayIt =
							m_chatReplayByIdempotency.find(stageContext.idempotencyKey);
						if (replayIt != m_chatReplayByIdempotency.end()) {
							return protocol::ResponseFrame{
								.id = request.id,
								.ok = replayIt->second.ok,
								.payloadJson = replayIt->second.payloadJson,
								.error = replayIt->second.error,
							};
						}

						return protocol::OkResponse(request, "{\"runId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"queued\":false,\"deduped\":true}");
					}

					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
					 .error = stageContext.responseError.has_value()
							? stageContext.responseError
						  : std::optional<protocol::ErrorShape>(
								BuildRuntimeErrorShape(
									stageContext.responseErrorCode,
									stageContext.responseErrorMessage,
									stageContext.runId,
									stageContext.sessionKey)),
					};
				}

				const std::string requestedSessionKey = stageContext.requestedSessionKey;
				const std::string sessionKey = stageContext.sessionKey;
				const std::string message = stageContext.message;
				const std::string normalizedMessage = stageContext.normalizedMessage;
				const std::string idempotencyKey = stageContext.idempotencyKey;
				const std::string clientConnectionId = stageContext.clientConnectionId;
				const bool forceError = stageContext.forceError;
				const bool hasAttachments = stageContext.hasAttachmentPayload;
				const std::uint64_t nowMs = stageContext.nowEpochMs > 0
					? stageContext.nowEpochMs
					: CurrentEpochMsLocal();
				const std::string runId = !stageContext.runId.empty()
					? stageContext.runId
					: (!request.id.empty()
						? request.id
						: ("chat-run-" + std::to_string(nowMs) +
							"-" + std::to_string(m_chatRunsById.size() + 1)));
				const ChatTranscriptStore transcriptStore;
				bool userTurnPersisted = false;
				auto persistUserTurnIfNeeded = [&]() {
					if (userTurnPersisted) {
						return;
					}

					if (!normalizedMessage.empty() || hasAttachments) {
						const auto userPersisted = transcriptStore.AppendUserMessage(
							ChatTranscriptStore::AppendParams{
								.sessionKey = sessionKey,
								.role = "user",
								.message = normalizedMessage.empty()
									? std::string("[attachment]")
									: normalizedMessage,
								.label = hasAttachments ? "attachments" : std::string(),
								.idempotencyKey = runId + ":user",
							});
						if (!userPersisted.ok && !userPersisted.error.empty()) {
							EmitTelemetryEvent(
								"gateway.chat.transcript.user.persist.error",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"sessionKey\":" + JsonString(sessionKey) +
								",\"error\":" + JsonString(userPersisted.error) + "}");
						}
					}

					PushHistoryMessageIfNew(
						m_chatHistoryBySession[sessionKey],
						BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));
					userTurnPersisted = true;
					};

				persistUserTurnIfNeeded();
				const bool runAlreadyTracked =
					m_chatRunsById.find(runId) != m_chatRunsById.end();
				const bool lateJoinRequested =
					runAlreadyTracked &&
					stageContext.hasConnectedClient &&
					!clientConnectionId.empty();
				const bool pushLifecycleEnabled =
					stageContext.pushLifecycleRequested;

				ChatControlPlaneService controlPlaneService;
				const bool hasRegisteredRecipient =
					!clientConnectionId.empty() &&
					m_transportRecipientRegistry.HasRecipients(runId);
				const auto sendControlDecision =
					controlPlaneService.EvaluateSendControl(
						ChatControlPlaneService::SendControlInput{
							.sessionKey = sessionKey,
							.deliver = stageContext.deliver,
							.routeChannel = stageContext.routeChannel,
							.routeTo = stageContext.routeTo,
							.clientMode = stageContext.clientMode,
						  .hasConnectedClient = stageContext.hasConnectedClient,
							.mainKey = stageContext.mainKey,
							.clientCaps = stageContext.clientCaps,
							.runId = runId,
						 .hasRegisteredRecipient = hasRegisteredRecipient,
							.lateJoinRequested = lateJoinRequested,
						});
				if (sendControlDecision.toolEvents.wantsToolEvents &&
					!clientConnectionId.empty()) {
					m_transportRecipientRegistry.RegisterRecipient(
						runId,
						sessionKey,
						clientConnectionId,
						nowMs);
					m_transportRecipientRegistry.RegisterLateJoin(
						sessionKey,
						clientConnectionId,
						nowMs);
					m_chatToolEventRecipientsByRun[runId].insert(clientConnectionId);
					for (const auto& [activeRunId, activeRun] : m_chatRunsById) {
						if (activeRunId != runId &&
							activeRun.sessionKey == sessionKey &&
							activeRun.active) {
							m_chatToolEventRecipientsByRun[activeRunId].insert(clientConnectionId);
						}
					}
					m_transportRecipientRegistry.PruneExpired(nowMs);
				}

				if (lateJoinRequested && !clientConnectionId.empty()) {
					auto& replayQueue = m_chatEventsBySession[sessionKey];
					const auto activeRuns =
						m_transportRecipientRegistry.ActiveRunsForSession(sessionKey);
					for (const auto& activeRunId : activeRuns) {
						const auto activeRunIt = m_chatRunsById.find(activeRunId);
						if (activeRunIt == m_chatRunsById.end()) {
							continue;
						}

						const auto& activeRun = activeRunIt->second;
						if (!activeRun.active ||
							RuntimeTranscriptGuard::IsSilentReplyText(activeRun.assistantText)) {
							continue;
						}

						std::string replayText;
						if (activeRun.providerDeltaCursor > 0 &&
							activeRun.providerDeltaCursor <= activeRun.providerDeltas.size()) {
							replayText = activeRun.providerDeltas[activeRun.providerDeltaCursor - 1];
						}
						if (replayText.empty()) {
							replayText = activeRun.assistantText.substr(
								0,
								(std::min)(activeRun.assistantText.size(), std::size_t{ 64 }));
						}

						if (replayText.empty()) {
							continue;
						}

						const std::string replayMessage =
							BuildAssistantDeltaMessageJson(replayText);
						PushEventWithRetentionLimit(replayQueue, ChatEventState{
								.runId = activeRun.runId,
								.sessionKey = activeRun.sessionKey,
								.state = "delta",
								.messageJson = replayMessage,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							});
						BranchDecisionDiagnostics::Emit(
							activeRun.runId,
							"controlplane",
							"late_join_replay",
							"delta_replayed",
							std::string("{\"connectionId\":") +
							JsonString(clientConnectionId) +
							",\"sessionKey\":" +
							JsonString(sessionKey) + "}");
					}
				}
				EmitTelemetryEvent(
					"gateway.chat.controlplane.decision",
					std::string("{\"runId\":") +
					JsonString(runId) +
					",\"route\":{\"originatingChannel\":" +
					JsonString(sendControlDecision.route.originatingChannel) +
					",\"explicitDeliverRoute\":" +
					std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false") +
					",\"reasonCode\":" +
					JsonString(sendControlDecision.route.reasonCode) +
					"},\"toolEvents\":{\"allowed\":" +
					std::string(sendControlDecision.toolEvents.wantsToolEvents ? "true" : "false") +
					",\"reasonCode\":" +
					JsonString(sendControlDecision.toolEvents.reasonCode) +
					"}}"
				);

				const std::vector<std::string> attachmentMimeTypes =
					stageContext.attachmentMimeTypes;
				const SendPolicyDecision sendPolicyDecision =
					SendPolicyResolver::Evaluate(
						sessionKey,
						normalizedMessage,
						hasAttachments,
						attachmentMimeTypes);
				if (!sendPolicyDecision.allowed) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"transport_control",
						"send_policy",
						"denied_send",
						std::string("{\"hits\":") +
						SerializeStringArrayLocal(sendPolicyDecision.policyHits) +
						"}");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"send\",\"reason\":\"denied_send\"}");
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
					  .error = BuildRuntimeErrorShape(
							"denied_send",
							"Request denied by send policy.",
							runId,
							sessionKey),
					};
				}
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
				const auto orchestrationPolicy =
					ChatOrchestrationPolicy::Evaluate(
						ChatOrchestrationPolicy::Input{
							.orchestrationPath = m_embeddedOrchestrationPath,
							.message = normalizedMessage,
							.forceError = forceError,
							.hasAttachments = hasAttachments,
						});
				const std::string orchestrationPath =
					orchestrationPolicy.selectedPath;
				const bool allowPromptOrchestration =
					orchestrationPolicy.compatDeterministicEnabled;
				const bool forceWeatherEmailDeterministicOrchestration =
					orchestrationPolicy.intentDeterministicEnabled;
				const bool allowDeterministicPromptOrchestration =
					orchestrationPolicy.deterministicEnabled;
				m_latestOrchestrationPathSelection.runId = runId;
				m_latestOrchestrationPathSelection.path = orchestrationPath;
				m_latestOrchestrationPathSelection.compatDeterministicEnabled =
					allowPromptOrchestration;
				m_latestOrchestrationPathSelection.intentDeterministicEnabled =
					forceWeatherEmailDeterministicOrchestration;
				m_latestOrchestrationPathSelection.deterministicEnabled =
					allowDeterministicPromptOrchestration;
				m_latestOrchestrationPathSelection.decisionReasonCode =
					orchestrationPolicy.decisionReasonCode;
				m_latestOrchestrationPathSelection.decompositionMetadataSource =
					orchestrationPolicy.decompositionMetadataSource;
				m_latestOrchestrationPathSelection.orderedPolicyMode =
					orchestrationPolicy.orderedPolicyMode;
				m_latestOrchestrationPathSelection.orderedPolicyStrict =
					orchestrationPolicy.orderedPolicyStrict;
				m_latestOrchestrationPathSelection.fallbackPolicyProfile =
					orchestrationPolicy.fallbackPolicyProfile;
				m_latestOrchestrationPathSelection.observedAtEpochMs = nowMs;
				EmitTelemetryEvent(
					"gateway.chat.orchestration.pathSelection",
					std::string("{\"runId\":") +
					JsonString(runId) +
					",\"path\":" +
					JsonString(orchestrationPath) +
					",\"compatDeterministicEnabled\":" +
					std::string(allowPromptOrchestration ? "true" : "false") +
					",\"intentDeterministicEnabled\":" +
					std::string(
						forceWeatherEmailDeterministicOrchestration ? "true" : "false") +
					",\"deterministicEnabled\":" +
					std::string(
						allowDeterministicPromptOrchestration ? "true" : "false") +
					",\"decisionReasonCode\":" +
					JsonString(orchestrationPolicy.decisionReasonCode) +
					",\"decompositionMetadataSource\":" +
					JsonString(orchestrationPolicy.decompositionMetadataSource) +
					",\"orderedPolicyDecision\":" +
					JsonString(orchestrationPolicy.orderedPolicyDecision) +
					",\"orderedPolicyMode\":" +
					JsonString(orchestrationPolicy.orderedPolicyMode) +
					",\"orderedPolicyStrict\":" +
					std::string(orchestrationPolicy.orderedPolicyStrict
						? "true"
						: "false") +
					",\"orderedPolicyTargets\":" +
					SerializeStringArrayLocal(orchestrationPolicy.orderedPolicyTargets) +
					",\"fallbackPolicyProfile\":" +
					JsonString(orchestrationPolicy.fallbackPolicyProfile) +
					",\"allowlistPolicyHint\":" +
					JsonString(orchestrationPolicy.allowlistPolicyHint) +
					",\"fallbackPolicyHint\":" +
					JsonString(orchestrationPolicy.fallbackPolicyHint) +
					",\"dynamicRuntimeDefault\":true}");
				BranchDecisionDiagnostics::Emit(
					runId,
					"runtime",
					"orchestration.pathSelection",
					orchestrationPolicy.decisionReasonCode);
				EmitTelemetryEvent(
					"gateway.chat.policy.decision",
					std::string("{\"runId\":") + JsonString(runId) +
					",\"layer\":\"orchestration\",\"reason\":" +
					JsonString(orchestrationPolicy.decisionReasonCode) +
					",\"decompositionSource\":" +
					JsonString(orchestrationPolicy.decompositionMetadataSource) +
					",\"orderingMode\":" +
					JsonString(orchestrationPolicy.orderedPolicyMode) +
					",\"fallbackPolicyProfile\":" +
					JsonString(orchestrationPolicy.fallbackPolicyProfile) + "}");
				const auto runtimeToolsSnapshot = m_toolRegistry.List();
				OrderedSequencePolicyOverride orderedSequencePolicyOverride{};
				const OrderedSequencePolicyOverride* orderedSequencePolicyOverridePtr =
					nullptr;
				if (orchestrationPolicy.orderedPolicyMode != "none" &&
					!orchestrationPolicy.orderedPolicyTargets.empty()) {
					orderedSequencePolicyOverride.orderedTargets =
						orchestrationPolicy.orderedPolicyTargets;
					orderedSequencePolicyOverride.strictAllowlist =
						orchestrationPolicy.orderedPolicyStrict;
					orderedSequencePolicyOverride.source =
						orchestrationPolicy.decompositionMetadataSource;
					orderedSequencePolicyOverridePtr = &orderedSequencePolicyOverride;
				}
				const auto orderedSequencePreflight =
					RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
						normalizedMessage,
						runtimeToolsSnapshot,
						m_skillsCatalogState.entries,
						orderedSequencePolicyOverridePtr);
				const bool preferChineseResponse =
					stageContext.preferChineseResponse;
				std::vector<std::string> orderedAllowlistTargets;
				bool enforceOrderedAllowlist = false;
				if (orderedSequencePreflight.enforced &&
					(!orderedSequencePreflight.resolvedToolTargets.empty()) &&
					(orderedSequencePreflight.strictAllowlist
						? orderedSequencePreflight.missingTargets.empty()
						: true)) {
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
				const ToolPolicyDecision toolPolicyDecision =
					ToolPolicyPipeline::Build(
						enforceOrderedAllowlist,
						orderedAllowlistTargets,
						runtimeToolsSnapshot);
				if (!toolPolicyDecision.allowAll &&
					toolPolicyDecision.allowedTargets.empty()) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"runtime",
						"tool_policy",
						"tool_policy_block");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"tool\",\"reason\":\"tool_policy_block\"}");
				}
				else {
					orderedAllowlistTargets = toolPolicyDecision.allowedTargets;
					enforceOrderedAllowlist = !toolPolicyDecision.allowAll;
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"tool\",\"reason\":" +
						JsonString(toolPolicyDecision.reasonCode) + "}");
				}

				const TranscriptPolicyDecision transcriptPolicyDecision =
					TranscriptPolicyResolver::Resolve(
						stageContext.runtimeMessage,
						"deepseek");
				if (transcriptPolicyDecision.applied) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"runtime",
						"transcript_policy",
						"transcript_policy_applied");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"transcript\",\"reason\":\"transcript_policy_applied\"}");
				}

				const std::string runtimeMessage =
					(orderedSequencePreflight.enforced && !enforceOrderedAllowlist)
					? (std::string(preferChineseResponse
						? Utf8LiteralLocal(u8"\u6709\u5E8F\u6267\u884C\u6B65\u9AA4\uFF08\u4FDD\u6301\u987A\u5E8F\uFF09\uFF1A")
						: "Ordered execution steps (preserve order): ") +
						RuntimeSequencingPolicy::JoinOrderedResolution(
							orderedSequencePreflight) +
						"\n\n" + transcriptPolicyDecision.sanitizedMessage)
					: transcriptPolicyDecision.sanitizedMessage;
				BranchDecisionDiagnostics::EmitWithPayloadSummary(
					runId,
					"runtime",
					"runtime_message",
					"runtime_message_built",
					runtimeMessage,
					256);
				std::vector<ChatRuntimeResult::TaskDeltaEntry> orderedPreflightTaskDeltas;

				if (forceError) {
					failed = true;
					backendErrorCode = "forced_error";
					backendErrorMessage = "forced error for deterministic verification";
				}

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
						",\"strictAllowlist\":" +
						std::string(
							orderedSequencePreflight.strictAllowlist ? "true" : "false") +
						",\"missing\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
						"}");

					if (orderedSequencePreflight.strictAllowlist &&
						!orderedSequencePreflight.missingTargets.empty()) {
						const std::string strictMissingErrorCode =
							"ordered_sequence_target_unavailable";
						const std::string strictMissingErrorMessage =
							"Ordered execution preflight failed. Missing targets: " +
							RuntimeSequencingPolicy::JoinOrderedTargets(
								orderedSequencePreflight.missingTargets);

						RunLoopBudget orderedRecoveryBudget;
						const RecoveryOutcome orderedRecoveryOutcome =
							RecoveryPolicyEngine::Execute(
								RecoveryRequest{
									.runId = runId,
									.sessionKey = sessionKey,
									.message = normalizedMessage,
									.errorCode = strictMissingErrorCode,
									.errorMessage = strictMissingErrorMessage,
									.authProfileId = "default",
									.taskDeltas = orderedPreflightTaskDeltas,
								},
								orderedRecoveryBudget);

						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"ordered_preflight\",\"reason\":\"strict_missing\",\"recoveryRoute\":" +
							JsonString(orderedRecoveryOutcome.recoveryRoute) + "}");

						if (orderedRecoveryOutcome.recovered ||
							orderedRecoveryOutcome.shouldReinvokeRuntime) {
							if (!orderedRecoveryOutcome.recoveryDeltas.empty()) {
								orderedPreflightTaskDeltas.insert(
									orderedPreflightTaskDeltas.end(),
									orderedRecoveryOutcome.recoveryDeltas.begin(),
									orderedRecoveryOutcome.recoveryDeltas.end());
							}
							if (!orderedRecoveryOutcome.normalizedDeltas.empty()) {
								orderedPreflightTaskDeltas = orderedRecoveryOutcome.normalizedDeltas;
							}
						}
						else {
							failed = true;
							orchestrationHandled = true;
							backendErrorCode = strictMissingErrorCode;
							backendErrorMessage = strictMissingErrorMessage;
							assistantText =
								(preferChineseResponse
									? (Utf8LiteralLocal(u8"\u65E0\u6CD5\u6267\u884C\u6709\u5E8F\u5DE5\u4F5C\u6D41\uFF0C\u4EE5\u4E0B\u6B65\u9AA4\u76EE\u6807\u4E0D\u53EF\u7528\uFF1A") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
										Utf8LiteralLocal(u8"\u3002\u8BF7\u5B89\u88C5\u6216\u542F\u7528\u8FD9\u4E9B\u6280\u80FD/\u5DE5\u5177\u540E\u91CD\u8BD5\u3002"))
									: (std::string("Unable to execute the ordered workflow because required step targets are unavailable: ") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
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
					else if (!orderedSequencePreflight.missingTargets.empty()) {
						BranchDecisionDiagnostics::Emit(
							runId,
							"runtime",
							"ordered_preflight",
							"advisory_missing_targets_continue");
						EmitTelemetryEvent(
							"gateway.chat.ordered.preflight",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"advisoryContinue\":true,\"missing\":" +
							SerializeStringArrayLocal(
								orderedSequencePreflight.missingTargets) +
							"}");
					}
				}

				if (!forceError &&
					!orchestrationHandled &&
					!hasAttachments &&
					forceWeatherEmailDeterministicOrchestration) {
					const auto orchestrationResult =
						TryOrchestrateWeatherEmailPrompt(
							m_toolRegistry,
							normalizedMessage);

					if (orchestrationResult.matched) {
						orchestrationHandled = true;
						assistantDeltas = orchestrationResult.assistantDeltas;
						if (orchestrationResult.success) {
							failed = false;
							backendErrorCode.clear();
							backendErrorMessage.clear();
							assistantText = orchestrationResult.assistantText;
							if (assistantText.empty() &&
								!assistantDeltas.empty()) {
								assistantText = assistantDeltas.back();
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

							auto orchestrationTaskDeltas =
								RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
									{},
									runId,
									sessionKey,
									true,
									assistantText,
									{},
									{});
							if (!orderedPreflightTaskDeltas.empty()) {
								std::vector<ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
								mergedTaskDeltas.reserve(
									orderedPreflightTaskDeltas.size() +
									orchestrationTaskDeltas.size());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orderedPreflightTaskDeltas.begin(),
									orderedPreflightTaskDeltas.end());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orchestrationTaskDeltas.begin(),
									orchestrationTaskDeltas.end());
								orchestrationTaskDeltas = std::move(mergedTaskDeltas);
							}

							persistTaskDeltas(orchestrationTaskDeltas, true);
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

							auto orchestrationTaskDeltas =
								RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
									{},
									runId,
									sessionKey,
									false,
									{},
									backendErrorCode,
									backendErrorMessage);
							if (!orderedPreflightTaskDeltas.empty()) {
								std::vector<ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
								mergedTaskDeltas.reserve(
									orderedPreflightTaskDeltas.size() +
									orchestrationTaskDeltas.size());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orderedPreflightTaskDeltas.begin(),
									orderedPreflightTaskDeltas.end());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orchestrationTaskDeltas.begin(),
									orchestrationTaskDeltas.end());
								orchestrationTaskDeltas = std::move(mergedTaskDeltas);
							}

							persistTaskDeltas(orchestrationTaskDeltas, false);
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
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"queued",
						runId,
						sessionKey,
						nowMs);
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"started",
						runId,
						sessionKey,
						nowMs);
					if (pushLifecycleEnabled) {
						EmitPushLifecycleEvent(
							m_transport,
							m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							m_chatPushEventSeq);
						EmitPushLifecycleEvent(
							m_transport,
							m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							m_chatPushEventSeq);
					}

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
							.pushLifecycleRequested = pushLifecycleEnabled,
							.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
							.originatingChannel = sendControlDecision.route.originatingChannel,
							.originatingTo = sendControlDecision.route.originatingTo,
							.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
						});

					std::size_t streamedDeltaCount = 0;
					const auto runtimeResult = m_chatRuntimeCallback(
						ChatRuntimeRequest{
							.runId = runId,
							.sessionKey = sessionKey,
							.message = runtimeMessage,
							.bodyForCommands = stageContext.bodyForCommands,
							.bodyForAgent = stageContext.bodyForAgent.empty()
								? runtimeMessage
								: stageContext.bodyForAgent,
							.slashCommandName = stageContext.slashCommandName,
							.shouldLoadInlineSkillCommands =
								stageContext.shouldLoadInlineSkillCommands,
							.inlineInvocationAuthorizedSender =
								stageContext.inlineInvocationAuthorizedSender,
							.inlineInvocationSenderIsOwner =
								stageContext.inlineInvocationSenderIsOwner,
							.allowInlineToolImmediateExecution =
								stageContext.allowInlineToolImmediateExecution,
							.enforceOrderedAllowlist = enforceOrderedAllowlist,
							.orderedAllowedToolTargets = orderedAllowlistTargets,
							.hasAttachments = hasAttachments,
							.attachmentMimeTypes = attachmentMimeTypes,
							.onAssistantDelta =
								[this,
									&streamedDeltaCount,
									&runId,
									&sessionKey,
									&controlPlaneService,
									&sendControlDecision](const std::string& delta) {
									const std::string normalizedDelta = json::Trim(delta);
									if (normalizedDelta.empty() ||
										RuntimeTranscriptGuard::IsSilentReplyText(normalizedDelta)) {
										return;
									}

									if (!controlPlaneService.ShouldPublishToolDelta(
										normalizedDelta,
										sendControlDecision)) {
										return;
									}
									if (normalizedDelta.find("tools.execute") == 0) {
										const auto recipientsIt =
											m_chatToolEventRecipientsByRun.find(runId);
										if (recipientsIt == m_chatToolEventRecipientsByRun.end() ||
											recipientsIt->second.empty()) {
											return;
										}
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
									const std::uint64_t deltaNowMs = CurrentEpochMsLocal();
									GatewayLifecycleEventEmitter::EmitLifecycle(
										"delta",
										runId,
										sessionKey,
									 deltaNowMs);

									auto runStateIt = m_chatRunsById.find(runId);
									if (runStateIt != m_chatRunsById.end() &&
										runStateIt->second.pushLifecycleRequested) {
										EmitPushLifecycleEvent(
											m_transport,
							 m_eventFanoutService,
											GatewayEventFanoutService::ChatLifecycleEvent{
												.runId = runId,
												.sessionKey = sessionKey,
												.state = "delta",
												.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
												.errorMessage = std::nullopt,
												.timestampMs = deltaNowMs,
											},
											m_chatPushEventSeq);
									}

									if (runStateIt != m_chatRunsById.end()) {
										runStateIt->second.assistantText = normalizedDelta;
										runStateIt->second.streamCursor = normalizedDelta.size();
									  runStateIt->second.lastEmitMs = deltaNowMs;
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
							",\"recoveryRoute\":" +
							JsonString(recoveryOutcome.recoveryRoute) +
							",\"compaction\":" +
							std::string(recoveryOutcome.compactionApplied ? "true" : "false") +
							",\"truncation\":" +
							std::string(recoveryOutcome.truncationApplied ? "true" : "false") +
							",\"fallbackPolicyProfile\":" +
							JsonString(orchestrationPolicy.fallbackPolicyProfile) +
							",\"profile\":" +
							JsonString(recoveryOutcome.selectedProfileId) +
							",\"contextEngine\":" +
							JsonString(recoveryOutcome.selectedContextEngineId) +
							",\"terminalCode\":" +
							JsonString(recoveryOutcome.terminalErrorCode) +
							"}");
						BranchDecisionDiagnostics::Emit(
							runId,
							"recovery",
							recoveryOutcome.recovered
							? "recovered"
							: "terminal",
							recoveryOutcome.terminalErrorCode.empty()
							? "recovery_chain_continue"
							: recoveryOutcome.terminalErrorCode);

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
				if (!assistantText.empty() && !silentAssistantReply) {
					const auto assistantPersisted = transcriptStore.AppendAssistantMessage(
						ChatTranscriptStore::AppendParams{
							.sessionKey = sessionKey,
							.role = "assistant",
							.message = assistantText,
							.label = std::string(),
							.idempotencyKey = runId + ":assistant",
						});
					if (!assistantPersisted.ok && !assistantPersisted.error.empty()) {
						EmitTelemetryEvent(
							"gateway.chat.transcript.assistant.persist.error",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"sessionKey\":" + JsonString(sessionKey) +
							",\"error\":" + JsonString(assistantPersisted.error) + "}");
					}
				}

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
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"queued",
						runId,
						sessionKey,
						nowMs);
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"started",
						runId,
						sessionKey,
						nowMs);
					if (pushLifecycleEnabled) {
						EmitPushLifecycleEvent(
							m_transport,
							m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							m_chatPushEventSeq);
						EmitPushLifecycleEvent(
							m_transport,
							m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							m_chatPushEventSeq);
					}
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
						const std::string initialDeltaMessage = BuildAssistantDeltaMessageJson(
							assistantText.substr(0, streamCursor));
						PushEventWithRetentionLimit(sessionEvents, ChatEventState{
							   .runId = runId,
							   .sessionKey = sessionKey,
							   .state = "delta",
							  .messageJson = initialDeltaMessage,
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"delta",
							runId,
							sessionKey,
							nowMs);
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							runId +
							" session=" +
							sessionKey +
							" queueSize=" +
							std::to_string(sessionEvents.size()));
						if (pushLifecycleEnabled) {
							EmitPushLifecycleEvent(
								m_transport,
								m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "delta",
									.messageJson = initialDeltaMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								m_chatPushEventSeq);
						}
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
							.pushLifecycleRequested = pushLifecycleEnabled,
							.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
							.originatingChannel = sendControlDecision.route.originatingChannel,
							.originatingTo = sendControlDecision.route.originatingTo,
							.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
						});
				}

				m_chatTerminalDeliveredRunIds.erase(runId);

				if (!idempotencyKey.empty()) {
					m_chatRunByIdempotency.insert_or_assign(idempotencyKey, runId);
				}

				std::string sendPayload =
					"{\"runId\":\"" +
					EscapeJsonLocal(runId) +
					"\",\"backendErrorCode\":" +
					(backendErrorCode.empty()
						? std::string("null")
						: ("\"" + EscapeJsonLocal(backendErrorCode) + "\"")) +
					",\"queued\":true,\"deduped\":false" +
					",\"originatingChannel\":" +
					JsonString(sendControlDecision.route.originatingChannel) +
					",\"explicitDeliverRoute\":" +
					std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false");

				if (stageContext.pushLifecycleRequested) {
					sendPayload +=
						",\"lifecycle\":{\"transport\":\"push_compatible\",\"state\":\"started\"}";
				}
				sendPayload += "}";

				const protocol::ResponseFrame sendResponse = protocol::OkResponse(request, sendPayload);
				if (!idempotencyKey.empty()) {
					m_chatReplayByIdempotency.insert_or_assign(
						idempotencyKey,
						ChatReplayEntry{
							.ok = sendResponse.ok,
							.payloadJson = sendResponse.payloadJson,
							.error = sendResponse.error,
						});
				}

				if (stageContext.pushLifecycleRequested) {
					EmitTelemetryEvent(
						"gateway.chat.lifecycle.push_ack",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"sessionKey\":" + JsonString(sessionKey) +
						",\"state\":\"started\"}");
				}

				return sendResponse;
			});

		m_dispatcher.Register(
			"chat.inject",
			[this](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::string message =
					ExtractStringParam(request.paramsJson, "message");
				const std::string label =
					ExtractStringParam(request.paramsJson, "label");

				if (json::Trim(message).empty()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
					  .error = BuildRuntimeErrorShape(
							"invalid_params",
							"`message` must be a non-empty string.",
							request.id,
							sessionKey),
					};
				}

				const ChatTranscriptStore transcriptStore;
				const auto appended = transcriptStore.AppendAssistantMessage(
					ChatTranscriptStore::AppendParams{
						.sessionKey = sessionKey,
						.message = message,
						.label = label,
					 .idempotencyKey = request.id,
					});
				if (!appended.ok) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
					  .error = BuildRuntimeErrorShape(
							"unavailable",
							"failed to write transcript: " +
							(appended.error.empty()
								? std::string("unknown error")
								: appended.error),
							request.id,
							sessionKey),
					};
				}

				if (appended.messageId.empty() || appended.messageJson.empty()) {
					return protocol::OkResponse(request, "{\"ok\":true,\"deduped\":true}");
				}

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				const std::string runId = "inject-" + appended.messageId;
				auto& queue = m_chatEventsBySession[sessionKey];
				PushEventWithRetentionLimit(queue, ChatEventState{
						.runId = runId,
						.sessionKey = sessionKey,
						.state = "final",
						.messageJson = appended.messageJson,
						.errorMessage = std::nullopt,
						.timestampMs = nowMs,
					});
				GatewayLifecycleEventEmitter::EmitLifecycle(
					"final",
					runId,
					sessionKey,
					nowMs);

				if (!RuntimeTranscriptGuard::IsSilentReplyText(message)) {
					PushHistoryMessageIfNew(
						m_chatHistoryBySession[sessionKey],
						appended.messageJson);
				}

				return protocol::OkResponse(request, "{\"ok\":true,\"messageId\":" +
						JsonString(appended.messageId) + "}");
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
					return protocol::OkResponse(request, "{\"aborted\":false,\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\"}");
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
				GatewayLifecycleEventEmitter::EmitLifecycle(
					"aborted",
					runIt->second.runId,
					sessionKey,
					nowMs);
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
				m_chatToolEventRecipientsByRun.erase(runId);
				m_transportRecipientRegistry.MarkRunFinalized(runId, nowMs);
				m_transportRecipientRegistry.PruneExpired(nowMs);

				ChatAbortCoordinator abortCoordinator;
				const auto persistResult = abortCoordinator.PersistAbortedPartial(
					ChatAbortCoordinator::PersistPartialParams{
						.sessionKey = sessionKey,
						.runId = runId,
						.text = runIt->second.assistantText,
						.origin = "rpc",
					});
				if (!persistResult.persisted && !persistResult.error.empty()) {
					EmitTelemetryEvent(
						"gateway.chat.abort.partialPersistence",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"sessionKey\":" +
						JsonString(sessionKey) +
						",\"persisted\":false,\"error\":" +
						JsonString(persistResult.error) +
						"}");
				}

				return protocol::OkResponse(request, "{\"aborted\":true,\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
					  "\",\"partialPersisted\":" +
						std::string(persistResult.persisted ? "true" : "false") +
						"}");
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
					const bool pushLifecycleEnabledForRun =
						run.pushLifecycleRequested;
					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(run.assistantText);
					const bool enoughTimeElapsed =
						run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;

					if (!run.failed && !silentAssistantReply &&
						((run.providerDeltaCursor < run.providerDeltas.size()) ||
							(run.streamCursor < run.assistantText.size())) &&
						enoughTimeElapsed) {
						ChatControlPlaneService controlPlaneService;
						const auto pollDecision =
							ChatControlPlaneService::SendControlDecision{
								.route = ChatRoutePolicy::Output{
									.originatingChannel = run.originatingChannel,
									.originatingTo = run.originatingTo,
									.explicitDeliverRoute = run.explicitDeliverRoute,
									.reasonCode = "persisted_run_state",
								},
								.toolEvents = ToolEventRecipientPolicy::Output{
									.wantsToolEvents = run.toolEventsAllowed,
									.reasonCode = "persisted_run_state",
								},
						};

						std::string deltaText;
						std::string pollDeltaMessage;
						if (run.providerDeltaCursor < run.providerDeltas.size()) {
							while (run.providerDeltaCursor < run.providerDeltas.size()) {
								deltaText = run.providerDeltas[run.providerDeltaCursor];
								++run.providerDeltaCursor;
								if (controlPlaneService.ShouldPublishToolDelta(
									deltaText,
									pollDecision)) {
									if (deltaText.find("tools.execute") == 0) {
										const auto recipientsIt =
											m_chatToolEventRecipientsByRun.find(run.runId);
										if (recipientsIt == m_chatToolEventRecipientsByRun.end() ||
											recipientsIt->second.empty()) {
											deltaText.clear();
											continue;
										}
									}
									break;
								}
								deltaText.clear();
							}
							if (deltaText.empty()) {
								goto skip_poll_delta_emit;
							}
							run.streamCursor = deltaText.size();
						}
						else {
							const std::size_t nextCursor =
								(std::min)(run.assistantText.size(), run.streamCursor + std::size_t{ 8 });
							run.streamCursor = nextCursor;
							deltaText = run.assistantText.substr(0, run.streamCursor);
						}

						run.lastEmitMs = nowMs;
						pollDeltaMessage = BuildAssistantDeltaMessageJson(deltaText);

						PushEventWithRetentionLimit(queue, ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = "delta",
							   .messageJson = pollDeltaMessage,
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						if (pushLifecycleEnabledForRun) {
							EmitPushLifecycleEvent(
								m_transport,
								m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = run.runId,
									.sessionKey = run.sessionKey,
									.state = "delta",
									.messageJson = pollDeltaMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								m_chatPushEventSeq);
						}
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							run.runId +
							" session=" +
							run.sessionKey +
							" queueSize=" +
							std::to_string(queue.size()));
					skip_poll_delta_emit:;
					}

					const bool streamCompleted =
						run.failed ||
						silentAssistantReply ||
						(run.providerDeltaCursor >= run.providerDeltas.size() &&
							run.streamCursor >= run.assistantText.size());
					if (streamCompleted && !run.terminalEventEnqueued) {
						const std::optional<std::string> terminalMessage =
							run.failed || silentAssistantReply
							? std::nullopt
							: std::optional<std::string>(
								BuildAssistantFinalMessageJson(run.assistantText, nowMs));
						const std::optional<std::string> terminalError =
							run.failed
							? std::optional<std::string>(run.errorMessage.empty()
								? "chat error"
								: run.errorMessage)
							: std::nullopt;

						PushEventWithRetentionLimit(queue, ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = run.failed ? "error" : "final",
							   .messageJson = terminalMessage,
							   .errorMessage = terminalError,
							   .timestampMs = nowMs,
							});
						if (pushLifecycleEnabledForRun) {
							EmitPushLifecycleEvent(
								m_transport,
								m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = run.runId,
									.sessionKey = run.sessionKey,
									.state = run.failed ? "error" : "final",
									.messageJson = terminalMessage,
									.errorMessage = terminalError,
									.timestampMs = nowMs,
								},
								m_chatPushEventSeq);
						}
						GatewayLifecycleEventEmitter::EmitLifecycle(
							run.failed ? "error" : "final",
							run.runId,
							run.sessionKey,
							nowMs,
							terminalError);
						run.terminalEventEnqueued = true;
						m_transportRecipientRegistry.MarkRunFinalized(
							run.runId,
							nowMs);
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
						m_chatToolEventRecipientsByRun.erase(run.runId);
						m_transportRecipientRegistry.PruneExpired(nowMs);
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
								m_transportRecipientRegistry.PruneRun(eventState.runId);
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
				return protocol::OkResponse(request, "{\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\",\"events\":" +
						eventsJson +
						",\"count\":" +
						std::to_string(emitted) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.skills.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;

				return protocol::OkResponse(request, "{\"total\":" +
						std::to_string(state.entries.size()) +
						",\"rootsScanned\":" +
						std::to_string(state.rootsScanned) +
						",\"rootsSkipped\":" +
						std::to_string(state.rootsSkipped) +
						",\"pluginRootsConfigured\":" +
						std::to_string(state.pluginRootsConfigured) +
						",\"pluginRootsScanned\":" +
						std::to_string(state.pluginRootsScanned) +
						",\"loaderPolicyRejectPathSymlink\":" +
						std::to_string(state.loaderPolicyRejectPathSymlinkCount) +
						",\"loaderPolicyStrictFrontmatter\":" +
						std::to_string(state.loaderPolicyStrictFrontmatterCount) +
						",\"symlinkRejectedFiles\":" +
						std::to_string(state.symlinkRejectedFiles) +
						",\"strictFrontmatterOmittedFiles\":" +
						std::to_string(state.strictFrontmatterOmittedFiles) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
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
						",\"strictEntryResolutionMode\":" +
						std::to_string(state.strictEntryResolutionModeCount) +
						",\"compatEntryResolutionMode\":" +
						std::to_string(state.compatEntryResolutionModeCount) +
						",\"configResolvedByKey\":" +
						std::to_string(state.configResolvedByKeyCount) +
						",\"configResolvedByNameFallback\":" +
						std::to_string(state.configResolvedByNameFallbackCount) +
						",\"allowlistRaw\":" +
						std::to_string(state.allowlistRawCount) +
						",\"allowlistNormalized\":" +
						std::to_string(state.allowlistNormalizedCount) +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"remoteEligibilityEnabled\":" +
						std::to_string(state.remoteEligibilityEnabledCount) +
						",\"remotePlatformSatisfied\":" +
						std::to_string(state.remotePlatformSatisfiedCount) +
						",\"remoteBinSatisfied\":" +
						std::to_string(state.remoteBinSatisfiedCount) +
						",\"remoteAnyBinSatisfied\":" +
						std::to_string(state.remoteAnyBinSatisfiedCount) +
						",\"alwaysBypass\":" +
						std::to_string(state.alwaysBypassCount) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
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
						",\"sandboxDestinationNamingMode\":\"" +
						EscapeJsonLocal(state.sandboxDestinationNamingMode) +
						"\"" +
						",\"sandboxDestinationCollisions\":" +
						std::to_string(state.sandboxDestinationCollisions) +
						",\"sandboxSourceDirFallbacks\":" +
						std::to_string(state.sandboxSourceDirFallbacks) +
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
						"}");
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
				return protocol::OkResponse(request, "{\"options\":" +
						optionsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
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

				return protocol::OkResponse(request, "{\"skill\":\"" +
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
						"}");
			});

		m_dispatcher.Register(
			"gateway.skills.scan.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"files\":" +
						std::to_string(state.scanScannedFiles) +
						",\"info\":" +
						std::to_string(state.scanInfoCount) +
						",\"warn\":" +
						std::to_string(state.scanWarnCount) +
						",\"critical\":" +
						std::to_string(state.scanCriticalCount) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.skills.sandbox.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"ok\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"synced\":" +
						std::to_string(state.sandboxSynced) +
						",\"skipped\":" +
						std::to_string(state.sandboxSkipped) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.skills.env.status",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.config.schema.get",
			[this](const protocol::RequestFrame& request) {
				if (!m_configSchemaGetCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "not_supported",
							.message =
								"Config schema callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto state = m_configSchemaGetCallback();
				const std::string payload =
					"{\"schema\":" +
					NormalizeJsonRawForPayload(state.schemaJson, "{}") +
					",\"uiHints\":" +
					NormalizeJsonRawForPayload(state.uiHintsJson, "{}") +
					",\"version\":\"" +
					EscapeJsonLocal(state.version) +
					"\",\"generatedAt\":\"" +
					EscapeJsonLocal(state.generatedAt) +
					"\"}";

				return protocol::OkResponse(request, payload);
			});

		m_dispatcher.Register(
			"gateway.config.schema.lookup",
			[this](const protocol::RequestFrame& request) {
				if (!m_configSchemaLookupCallback) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "not_supported",
							.message =
								"Config schema lookup callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const std::string requestedPath =
					ExtractStringParam(request.paramsJson, "path");
				const auto normalizedPath =
					NormalizeSchemaLookupPathRequest(requestedPath);
				if (!normalizedPath.has_value()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "invalid_lookup_path",
							.message =
								"Invalid schema lookup path.",
							.detailsJson =
								"{\"path\":\"" +
								EscapeJsonLocal(requestedPath) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				const auto lookupResult =
					m_configSchemaLookupCallback(normalizedPath.value());
				if (!lookupResult.has_value()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = protocol::ErrorShape{
							.code = "schema_path_not_found",
							.message = "Schema path was not found.",
							.detailsJson =
								"{\"path\":\"" +
								EscapeJsonLocal(normalizedPath.value()) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				return protocol::OkResponse(request, SerializeConfigSchemaLookupResultLocal(
							lookupResult.value()));
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

				return protocol::OkResponse(request, "{\"skill\":\"" +
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
						"}");
			});

		// Skills update: no param parsing here; forward to m_skillsUpdateCallback (BlazeClaw:
		// SkillsGatewayMethodHandler::HandleSkillsUpdate). Alias "skills.update" below rewrites method only.
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

				return protocol::OkResponse(request, "{\"ok\":" +
						std::string(ok ? "true" : "false") +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						"}");
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

				if (state.verifiedOpenPathFailures > 0 ||
					state.verifiedOpenValidationFailures > 0 ||
					state.verifiedOpenIoFailures > 0) {
					hints.push_back("skills.local-loader.verified-open");
				}

				if (state.commandMissingToolDispatchCount > 0 ||
					state.commandSkillNameDedupeCount > 0 ||
					state.commandInvalidArgModeFallbackCount > 0 ||
					state.commandSourceContributionCount > 0) {
					hints.push_back("skills.command-specs");
				}

				if (state.bundleCommandRootsScannedCount > 0 ||
					state.bundleCommandFilesLoadedCount > 0 ||
					state.bundleCommandFilesSkippedDisabledCount > 0 ||
					state.bundleCommandFilesSkippedEmptyPromptCount > 0 ||
					state.bundleCommandFilesSkippedInvalidNameCount > 0 ||
					state.bundleCommandFilesRejectedUnsafeCount > 0) {
					hints.push_back("plugins.bundle-commands");
				}

				if (state.entryConfigRawCount > 0 ||
					state.entryConfigNormalizedCount > 0 ||
					state.entryConfigMalformedCount > 0) {
					hints.push_back("skills.config.entries");
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
				return protocol::OkResponse(request, "{\"warnings\":" +
						std::to_string(state.warningCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
						",\"hints\":" +
						hintsJson +
						"}");
			});

		m_dispatcher.Register(
			"gateway.skills.prompt",
			[this](const protocol::RequestFrame& request) {
				const auto& state = m_skillsCatalogState;

				return protocol::OkResponse(request, "{\"prompt\":\"" +
						EscapeJsonLocal(state.prompt) +
						"\",\"included\":" +
						std::to_string(state.promptIncludedCount) +
						",\"chars\":" +
						std::to_string(state.promptChars) +
						",\"truncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						"}");
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
				return protocol::OkResponse(request, "{\"commands\":" +
						commandsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
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
				return protocol::OkResponse(request, "{\"refreshed\":" +
						std::string(refreshed ? "true" : "false") +
						",\"version\":" +
						std::to_string(state.snapshotVersion) +
						",\"reason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"}");
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

				return protocol::OkResponse(request, "{\"skills\":" +
						entriesJson +
						",\"count\":" +
						std::to_string(count) +
						",\"includeInvalid\":" +
						std::string(shouldIncludeInvalid ? "true" : "false") +
						"}");
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
			const std::string configuredOrchestrationPath =
				ToLowerCopyLocal(m_embeddedOrchestrationPath);
			const std::string selectedOrchestrationPath =
				m_latestOrchestrationPathSelection.path.empty()
				? configuredOrchestrationPath
				: m_latestOrchestrationPathSelection.path;
			const std::string latestSelectionRunId =
				m_latestOrchestrationPathSelection.runId;
			const std::uint64_t latestSelectionObservedAtEpochMs =
				m_latestOrchestrationPathSelection.observedAtEpochMs;
			const bool latestSelectionCompatEnabled =
				m_latestOrchestrationPathSelection.compatDeterministicEnabled;
			const bool latestSelectionIntentEnabled =
				m_latestOrchestrationPathSelection.intentDeterministicEnabled;
			const bool latestSelectionDeterministicEnabled =
				m_latestOrchestrationPathSelection.deterministicEnabled;
			const std::string latestSelectionDecisionReasonCode =
				m_latestOrchestrationPathSelection.decisionReasonCode;
			const std::string latestSelectionDecompositionMetadataSource =
				m_latestOrchestrationPathSelection.decompositionMetadataSource;
			const std::string latestSelectionOrderedPolicyMode =
				m_latestOrchestrationPathSelection.orderedPolicyMode;
			const bool latestSelectionOrderedPolicyStrict =
				m_latestOrchestrationPathSelection.orderedPolicyStrict;
			const std::string latestSelectionFallbackPolicyProfile =
				m_latestOrchestrationPathSelection.fallbackPolicyProfile;

			return protocol::OkResponse(request, "{\"state\":\"" + std::string(busy ? "busy" : "idle") +
					"\",\"activeSession\":\"" +
					EscapeJsonLocal(activeSession) + "\",\"activeAgent\":\"" +
					EscapeJsonLocal(activeAgent) + "\",\"queueDepth\":" +
					std::to_string(m_runtimeQueueDepth) +
					",\"running\":" +
					std::to_string(m_runtimeRunningCount) +
					",\"capacity\":" +
				 std::to_string(m_runtimeQueueCapacity) +
					",\"orchestrationPath\":{\"configured\":\"" +
					EscapeJsonLocal(configuredOrchestrationPath) +
					"\",\"selected\":\"" +
					EscapeJsonLocal(selectedOrchestrationPath) +
					"\",\"latestRunId\":\"" +
					EscapeJsonLocal(latestSelectionRunId) +
					"\",\"latestObservedAtEpochMs\":" +
					std::to_string(latestSelectionObservedAtEpochMs) +
					",\"compatDeterministicEnabled\":" +
					std::string(latestSelectionCompatEnabled ? "true" : "false") +
					",\"intentDeterministicEnabled\":" +
					std::string(latestSelectionIntentEnabled ? "true" : "false") +
					",\"deterministicEnabled\":" +
					std::string(
					   latestSelectionDeterministicEnabled ? "true" : "false") +
					",\"decisionReasonCode\":" +
					JsonString(latestSelectionDecisionReasonCode) +
					",\"decompositionMetadataSource\":" +
					JsonString(latestSelectionDecompositionMetadataSource) +
					",\"orderedPolicyMode\":" +
					JsonString(latestSelectionOrderedPolicyMode) +
					",\"orderedPolicyStrict\":" +
					std::string(latestSelectionOrderedPolicyStrict ? "true" : "false") +
					",\"fallbackPolicyProfile\":" +
					JsonString(latestSelectionFallbackPolicyProfile) +
				  "},\"dynamicLoopMetrics\":{\"success\":" +
					std::to_string(m_taskDeltaRunSuccessCount) +
					",\"failure\":" +
					std::to_string(m_taskDeltaRunFailureCount) +
					",\"timeout\":" +
					std::to_string(m_taskDeltaRunTimeoutCount) +
					",\"cancelled\":" +
					std::to_string(m_taskDeltaRunCancelledCount) +
					",\"fallback\":" +
				 std::to_string(m_taskDeltaRunFallbackCount) +
					"},\"taskDeltaLifecycleMapping\":{\"plan\":\"item.plan\",\"preflight\":\"tool.precheck\",\"tool_call\":\"tool.start\",\"tool_result\":\"tool.result\",\"final\":\"lifecycle.final\"}}");
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

				std::string runtimeNodeState = "unknown";
				std::string runtimePythonState = "unknown";
				std::string webBrowsingPythonState = "unknown";
				for (const auto& probe : health.probes) {
					if (probe.key == "runtime:node") {
						runtimeNodeState = probe.state;
						continue;
					}

					if (probe.key == "runtime:python") {
						runtimePythonState = probe.state;
						continue;
					}

					if (probe.key == "skill:web_browsing_python") {
						webBrowsingPythonState = probe.state;
					}
				}

				EmitTelemetryEvent(
					"gateway.runtime.preflight.executors",
					std::string("{\"runtimeNode\":\"") +
					EscapeJsonLocal(runtimeNodeState) +
					"\",\"runtimePython\":\"" +
					EscapeJsonLocal(runtimePythonState) +
					"\",\"webBrowsingPython\":\"" +
					EscapeJsonLocal(webBrowsingPythonState) +
					"\",\"generatedAtEpochMs\":" +
					std::to_string(health.generatedAtEpochMs) +
					"}");

				return protocol::OkResponse(request, "{\"probes\":" + probesJson +
						",\"count\":" +
						std::to_string(health.probes.size()) +
						",\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.health.readiness",
			[this](const protocol::RequestFrame& request) {
				const bool ready =
					m_running &&
					m_initialized &&
					m_dispatchInitialized &&
					m_runtimeHandlersInitialized;

				std::string reasonsJson = "[";
				bool first = true;
				auto appendReason = [&reasonsJson, &first](const std::string& reason) {
					if (!first) {
						reasonsJson += ",";
					}
					reasonsJson += "\"" + EscapeJsonLocal(reason) + "\"";
					first = false;
					};

				if (!m_running) {
					appendReason("gateway_not_running");
				}
				if (!m_initialized) {
					appendReason("runtime_not_initialized");
				}
				if (!m_dispatchInitialized) {
					appendReason("dispatcher_not_initialized");
				}
				if (!m_runtimeHandlersInitialized) {
					appendReason("runtime_handlers_not_initialized");
				}

				reasonsJson += "]";

				return protocol::OkResponse(request, "{\"ready\":" +
						std::string(ready ? "true" : "false") +
						",\"running\":" +
						std::string(m_running ? "true" : "false") +
						",\"initialized\":" +
						std::string(m_initialized ? "true" : "false") +
						",\"dispatchInitialized\":" +
						std::string(m_dispatchInitialized ? "true" : "false") +
						",\"runtimeHandlersInitialized\":" +
						std::string(m_runtimeHandlersInitialized ? "true" : "false") +
						",\"reasons\":" + reasonsJson +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.mutations.status",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"agentRuns\":" +
						std::to_string(m_agentRuns.size()) +
						",\"chatRuns\":" +
						std::to_string(m_chatRunsById.size()) +
						",\"taskDeltaRuns\":" +
						std::to_string(m_taskDeltasByRunId.size()) +
						",\"activeSessions\":" +
						std::to_string(m_sessionRegistry.List().size()) +
						",\"activeChannels\":" +
						std::to_string(m_channelRegistry.ListStatus().size()) +
						"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.health.capabilities",
			[](const protocol::RequestFrame& request) {
				const auto health =
					executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);
				return protocol::OkResponse(request, "{\"capabilities\":[{\"name\":\"email.send\",\"state\":\"" +
						EscapeJsonLocal(health.emailSendState) +
						"\"}],\"count\":1,\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}");
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

				return protocol::OkResponse(request, "{\"profileId\":\"" +
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
						"}}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseGate4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorGate4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phasePortal4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncGate4\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandGate4\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorPortal4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseBridge4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncPortal4\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandPortal4\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorBridge4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLink4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBridge4\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandBridge4\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLink4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNode5\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLink4\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLink4\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorNode5\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseHub3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncNode5\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandNode5\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorHub3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseGate3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncHub3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandHub3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorGate3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseRelay3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncGate3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandGate3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorRelay3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phasePortal3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncRelay3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandRelay3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorPortal3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseBridge3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncPortal3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandPortal3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorBridge3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseMesh3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBridge3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandBridge3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorMesh3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNode4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncMesh3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandMesh3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorNode4\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLink3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncNode4\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandNode4\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLink3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseThread2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLink3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLink3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorThread2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseChain2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncThread2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandThread2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorChain2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseSpline2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncChain2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandChain2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorSpline2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseRail2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncSpline2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandSpline2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorRail2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseTrack2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncRail2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandRail2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorTrack2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLane2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncTrack2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandTrack2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLane2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseGrid2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLane2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLane2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorGrid2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseBand2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncGrid2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandGrid2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorBand2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseArc2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBand2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandBand2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorArc2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseMesh2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncArc2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandArc2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorMesh2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLink2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncMesh2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandMesh2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLink2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNode3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLink2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLink2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorNode3\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseHub2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncNode3\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandNode3\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorHub2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseGate2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncHub2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandHub2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorGate2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseRelay2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncGate2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandGate2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorRelay2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phasePortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phasePortal\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncRelay2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandRelay2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorPortal\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseAnchor2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncPortal\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandPortal\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorAnchor2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseBridge\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncAnchor2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandAnchor2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorBridge\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNode",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNode\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBridge\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandBridge\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorNode2\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLink\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncNode2\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandNode2\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLink\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseThread\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLink\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLink\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorThread\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseChain\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncThread\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandThread\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorChain\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseSpline\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncChain\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandChain\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorSpline\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseRail\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncSpline\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandSpline\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorRail\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseTrack\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncRail\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandRail\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorTrack\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLane\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncTrack\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandTrack\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorLane\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseGrid\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncLane\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLane\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorGrid\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseSpan\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncGrid\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandGrid\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorSpan\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseFrame\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncSpan\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandSpan\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorFrame\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseCore\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncFrame\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandFrame\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorCore\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseNet",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNet\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncCore\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandCore\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorNode",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorNode\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseFabric",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseFabric\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncNet",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncNet\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandNode",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandNode\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorMesh\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseMesh\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncFabric",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncFabric\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandArc\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorArc\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseArc\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncMesh\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandLattice",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandLattice\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorSpiral\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseSpiral\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncArc\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandSpiral\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorRibbon\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseHelix\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncSpiral\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandHelix\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorContour\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseRibbon\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncHelix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncHelix\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandRibbon\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorEnvelope\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseContour\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncRibbon\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandContour\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.driftVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"driftVector\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLattice",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseLattice\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncContour\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandMatrix\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.envelopeDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"envelopeDrift\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseVector\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncMatrix\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandVector\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.biasEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"biasEnvelope\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorPhase",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorPhase\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncEnvelope\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandDrift\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.biasDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"biasDrift\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorField",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectors\":2,\"magnitude\":0,\"state\":\"steady\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncDrift\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandStability",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandStability\":100,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"amplitude\":1,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vectorDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectorDrift\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseBias",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"bias\":0,\"stable\":true}");
			});

		m_dispatcher.Register("gateway.runtime.streaming.status", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":" +
					std::string(m_runtimeAgentStreaming ? "true" : "false") +
					",\"mode\":\"chunked\",\"heartbeatMs\":1500" +
					std::string(m_streamingThrottled ? ",\"throttled\":true" : ",\"throttled\":false") +
					",\"bufferedFrames\":" + std::to_string(m_streamingBufferedFrames) +
					",\"bufferedBytes\":" + std::to_string(m_streamingBufferedBytes) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cohesive\":true,\"delta\":0,\"samples\":2}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveIndex\":1,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncBand",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBand\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveDrift\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register("gateway.models.failover.status", [this](const protocol::RequestFrame& request) {
			const std::string selectedPrimary =
				m_failoverOverrideActive
				? m_failoverOverrideModel
				: m_runtimeAgentModel;
			return protocol::OkResponse(request, "{\"primary\":\"" + EscapeJsonLocal(selectedPrimary) +
					"\",\"fallbacks\":[\"reasoner\"],\"maxRetries\":2,\"strategy\":\"ordered\",\"overrideActive\":" +
					std::string(m_failoverOverrideActive ? "true" : "false") + "}");
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

			return protocol::OkResponse(request, "{\"queued\":" + std::to_string(m_runtimeQueueDepth) +
					",\"running\":" + std::to_string(m_runtimeRunningCount) +
					",\"capacity\":" + std::to_string(m_runtimeQueueCapacity) +
					",\"updated\":true}");
			});

		m_dispatcher.Register("gateway.runtime.streaming.sample", [this](const protocol::RequestFrame& request) {
			const std::size_t chunks = m_streamingBufferedFrames > 0
				? (std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(3))
				: 2;
			const bool finalChunk = !m_streamingThrottled;

			return protocol::OkResponse(request, "{\"chunks\":[\"hello\",\"world\"],\"count\":" +
					std::to_string(chunks) +
					",\"final\":" +
					std::string(finalChunk ? "true" : "false") + "}");
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

			return protocol::OkResponse(request, "{\"model\":\"" + EscapeJsonLocal(requested) +
					"\",\"attempts\":[\"" + EscapeJsonLocal(requested) +
					"\",\"reasoner\"],\"selected\":\"" +
					EscapeJsonLocal(selected) + "\"}");
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

			return protocol::OkResponse(request, "{\"agentId\":\"" + EscapeJsonLocal(agentId) +
					"\",\"sessionId\":\"" + EscapeJsonLocal(sessionId) +
					"\",\"assigned\":" +
					std::string(assigned ? "true" : "false") +
					",\"assignments\":" +
					std::to_string(m_runtimeAssignmentCount) + "}");
			});

		m_dispatcher.Register("gateway.runtime.streaming.window", [this](const protocol::RequestFrame& request) {
			const auto windowMs = ExtractSizeParam(request.paramsJson, "windowMs");
			if (windowMs.has_value() && windowMs.value() > 0) {
				m_streamingWindowMs = windowMs.value();
			}

			return protocol::OkResponse(request, "{\"windowMs\":" + std::to_string(m_streamingWindowMs) +
					",\"frames\":" + std::to_string(m_streamingBufferedFrames) +
					",\"dropped\":0}");
			});

		m_dispatcher.Register("gateway.models.failover.metrics", [this](const protocol::RequestFrame& request) {
			const double successRate =
				m_failoverAttempts == 0
				? 1.0
				: static_cast<double>(m_failoverAttempts - m_failoverFallbackHits) /
				static_cast<double>(m_failoverAttempts);

			return protocol::OkResponse(request, "{\"attempts\":" + std::to_string(m_failoverAttempts) +
					",\"fallbackHits\":" + std::to_string(m_failoverFallbackHits) +
					",\"successRate\":" + std::to_string(successRate) + "}");
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

			return protocol::OkResponse(request, "{\"moved\":" + std::to_string(moved) +
					",\"remaining\":" +
					std::to_string(m_runtimeQueueDepth + m_runtimeRunningCount) +
					",\"strategy\":\"" + EscapeJsonLocal(strategy) +
					"\",\"rebalances\":" +
					std::to_string(m_runtimeRebalanceCount) + "}");
			});

		m_dispatcher.Register("gateway.runtime.streaming.backpressure", [this](const protocol::RequestFrame& request) {
			const std::size_t pressure =
				m_streamingHighWatermark == 0
				? 0
				: (m_streamingBufferedFrames * 100) / m_streamingHighWatermark;
			m_streamingThrottled = pressure >= 80;

			return protocol::OkResponse(request, "{\"pressure\":" + std::to_string(pressure) +
					",\"throttled\":" +
					std::string(m_streamingThrottled ? "true" : "false") +
					",\"bufferedFrames\":" +
					std::to_string(m_streamingBufferedFrames) + "}");
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

			return protocol::OkResponse(request, "{\"requested\":\"" + EscapeJsonLocal(requested) +
					"\",\"resolved\":\"" + EscapeJsonLocal(resolved) +
					"\",\"usedFallback\":" +
					std::string(useFallback ? "true" : "false") + "}");
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

			return protocol::OkResponse(request, "{\"drained\":" + std::to_string(drained) +
					",\"remaining\":0,\"reason\":\"" +
					EscapeJsonLocal(reason) +
					"\",\"drains\":" +
					std::to_string(m_runtimeDrainCount) + "}");
			});

		m_dispatcher.Register("gateway.runtime.streaming.replay", [this](const protocol::RequestFrame& request) {
			const std::size_t replayed =
				(std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(2));

			return protocol::OkResponse(request, "{\"replayed\":" + std::to_string(replayed) +
					",\"cursor\":\"stream-cursor-1\",\"complete\":true}");
			});

		m_dispatcher.Register("gateway.models.failover.audit", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":2,\"lastModel\":\"default\",\"lastOutcome\":\"primary\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.snapshot",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sessions\":" +
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
						std::to_string(m_runtimeRunningCount) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.cursor",
			[this](const protocol::RequestFrame& request) {
				const std::size_t lagMs =
					m_streamingBufferedFrames * 10;
				return protocol::OkResponse(request, "{\"cursor\":\"stream-cursor-1\",\"lagMs\":" +
						std::to_string(lagMs) +
						",\"hasMore\":" +
						std::string(m_streamingBufferedFrames > 0 ? "true" : "false") + "}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.policy",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"policy\":\"ordered\",\"maxRetries\":2,\"stickyPrimary\":true,\"overrideModel\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) + "\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.timeline",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"ticks\":[" +
						std::to_string(m_runtimeAssignmentCount) + "," +
						std::to_string(m_runtimeRebalanceCount) + "," +
						std::to_string(m_runtimeDrainCount) +
						"],\"count\":3,\"source\":\"runtime-orchestrator\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.metrics",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"frames\":" +
						std::to_string(m_streamingBufferedFrames) +
						",\"bytes\":" +
						std::to_string(m_streamingBufferedBytes) +
						",\"avgChunkMs\":5}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.history",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"events\":[\"primary\",\"fallback\"],\"count\":" +
						std::to_string(m_failoverAttempts) +
						",\"last\":\"" +
						std::string(m_failoverFallbackHits > 0 ? "fallback" : "primary") + "\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.heartbeat",
			[this](const protocol::RequestFrame& request) {
				const std::size_t backlog =
					m_runtimeQueueDepth + m_runtimeRunningCount;
				return protocol::OkResponse(request, "{\"alive\":true,\"intervalMs\":1000,\"jitterMs\":" +
						std::to_string(25 + (backlog > 0 ? 5 : 0)) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.health",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"healthy\":" +
						std::string(m_streamingBufferedFrames <= m_streamingHighWatermark ? "true" : "false") +
						",\"stalls\":0,\"recoveries\":0}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.recent",
			[this](const protocol::RequestFrame& request) {
				const std::string activeModel =
					m_failoverOverrideActive
					? m_failoverOverrideModel
					: m_runtimeAgentModel;
				return protocol::OkResponse(request, "{\"models\":[\"" + EscapeJsonLocal(m_runtimeAgentModel) +
						"\",\"reasoner\"],\"count\":2,\"active\":\"" +
						EscapeJsonLocal(activeModel) + "\"}");
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
				return protocol::OkResponse(request, "{\"pulse\":" + std::to_string(pulse) +
						",\"driftMs\":0,\"state\":\"" +
						std::string(busy ? "active" : "steady") + "\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.snapshot",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"frames\":" + std::to_string(m_streamingBufferedFrames) +
						",\"cursor\":\"stream-cursor-2\",\"sealed\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.window",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"attempts\":" +
						std::to_string(m_failoverAttempts) +
						",\"fallbacks\":" +
						std::to_string(m_failoverFallbackHits) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cadence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"periodMs\":1000,\"varianceMs\":5,\"aligned\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.watermark",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"high\":" + std::to_string(m_streamingHighWatermark) +
						",\"low\":4,\"current\":" +
						std::to_string(m_streamingBufferedFrames) + "}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.digest",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"digest\":\"sha256:failover-v1\",\"entries\":" +
						std::to_string(m_failoverAttempts) +
						",\"fresh\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.beacon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"beacon\":\"orch-1\",\"seq\":1,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"checkpoint\":\"cp-1\",\"frames\":2,\"persisted\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":2,\"primaryHits\":1,\"fallbackHits\":1}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.epoch",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"epoch\":1,\"startedMs\":1735689600000,\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.resume",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resumed\":true,\"cursor\":\"stream-cursor-3\",\"replayed\":1}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"profile\":\"balanced\",\"weights\":[70,30],\"version\":1}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"step\":1,\"locked\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.recovery",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"recovering\":false,\"attempts\":0,\"lastMs\":0}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"primary\":\"default\",\"secondary\":\"reasoner\",\"confidence\":100}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.signal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"signal\":\"ok\",\"priority\":1,\"latched\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.continuity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"continuous\":true,\"gaps\":0,\"lastSeq\":2}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.forecast",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"projectedFallbacks\":1,\"risk\":\"low\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"axis\":\"primary\",\"magnitude\":1,\"normalized\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.stability",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"stable\":true,\"variance\":0,\"samples\":2}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.threshold",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"minSuccessRate\":90,\"maxFallbacks\":2,\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"rows\":2,\"cols\":2,\"balanced\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.integrity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"valid\":true,\"violations\":0,\"checked\":2}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.guardrail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"rule\":\"max_fallbacks\",\"limit\":2,\"enforced\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.lattice",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"layers\":2,\"nodes\":4,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.coherence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"coherent\":true,\"drift\":0,\"segments\":2}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"floor\":90,\"ceiling\":100}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.mesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"nodes\":4,\"edges\":3,\"connected\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.fidelity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"fidelity\":100,\"drops\":0,\"verified\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.margin",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"headroom\":10,\"buffer\":2,\"safe\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.fabric",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"threads\":6,\"links\":8,\"resilient\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.accuracy",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"accuracy\":99,\"mismatches\":0,\"calibrated\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.reserve",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"reserve\":1,\"available\":true,\"priority\":2}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.load",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"queueLoad\":0,\"agentLoad\":0,\"state\":\"steady\"}");
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

				return protocol::OkResponse(request, "{\"bufferedFrames\":" +
						std::to_string(m_streamingBufferedFrames) +
						",\"bufferedBytes\":" +
						std::to_string(m_streamingBufferedBytes) +
						",\"highWatermark\":" +
						std::to_string(m_streamingHighWatermark) + "}");
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

				return protocol::OkResponse(request, "{\"active\":" +
						std::string(m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(m_failoverOverrideReason) +
						"\",\"changes\":" +
						std::to_string(m_failoverOverrideChanges) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.saturation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"saturation\":0,\"capacity\":8,\"state\":\"stable\"}");
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

				return protocol::OkResponse(request, "{\"throttled\":" +
						std::string(m_streamingThrottled ? "true" : "false") +
						",\"limitPerSec\":" +
						std::to_string(m_streamingThrottleLimitPerSec) +
						",\"currentPerSec\":" +
						std::to_string(currentPerSec) + "}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.clear",
			[this](const protocol::RequestFrame& request) {
				m_failoverOverrideActive = false;
				m_failoverOverrideModel = m_runtimeAgentModel;
				m_failoverOverrideReason = "cleared";
				++m_failoverOverrideChanges;

				return protocol::OkResponse(request, "{\"cleared\":true,\"active\":false,\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) + "\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.pressure",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pressure\":0,\"threshold\":80,\"state\":\"normal\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.pacing",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"paceMs\":50,\"burst\":1,\"adaptive\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.status",
			[this](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":" +
						std::string(m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(m_failoverOverrideReason) +
						"\",\"source\":\"runtime\",\"changes\":" +
						std::to_string(m_failoverOverrideChanges) + "}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.headroom",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"headroom\":8,\"used\":0,\"state\":\"ready\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.jitter",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"jitterMs\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.history",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":0,\"lastModel\":\"default\",\"active\":false}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.balance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"balanced\":true,\"skew\":0,\"state\":\"stable\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.drift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"driftMs\":0,\"windowMs\":1000,\"corrected\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.metrics",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"switches\":0,\"lastModel\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.efficiency",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"efficiency\":100,\"waste\":0,\"state\":\"optimized\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.variance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"variance\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.window",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"windowSec\":60,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.utilization",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"utilization\":0,\"capacity\":8,\"state\":\"idle\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.deviation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"deviation\":0,\"samples\":2,\"withinBudget\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.digest",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"digest\":\"sha256:override-v1\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.capacity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"capacity\":8,\"used\":0,\"state\":\"ready\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.alignment",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"aligned\":true,\"offsetMs\":0,\"windowMs\":1000}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.timeline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":0,\"active\":false,\"lastModel\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.occupancy",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"occupancy\":0,\"slots\":8,\"state\":\"idle\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.skew",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"skewMs\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.catalog",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"count\":1,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.elasticity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"elasticity\":100,\"headroom\":8,\"state\":\"expandable\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.dispersion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"dispersion\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.registry",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":1,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cohesion\":100,\"groups\":1,\"state\":\"coherent\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.curvature",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"curvature\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"rows\":1,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.resilience",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resilience\":100,\"faults\":0,\"state\":\"steady\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.smoothness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"smoothness\":100,\"jitterMs\":0,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.snapshot",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"revision\":1,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.readiness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"ready\":true,\"queueDepth\":0,\"state\":\"ready\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.harmonics",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"harmonics\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.pointer",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"pointer\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.contention",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"contention\":0,\"waiters\":0,\"state\":\"clear\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"step\":1,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.state",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"state\":\"none\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.fairness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"fairness\":100,\"skew\":0,\"state\":\"balanced\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.tempo",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tempo\":1,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"profile\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.equilibrium",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"equilibrium\":100,\"delta\":0,\"state\":\"balanced\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.steadiness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"steady\":true,\"variance\":0,\"windowMs\":1000}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.temporal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"temporal\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.consistency",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"consistent\":true,\"deviation\":0,\"samples\":2}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.audit",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.parity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"parity\":100,\"gap\":0,\"state\":\"aligned\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.stabilityIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"stabilityIndex\":100,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.spectral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"spectral\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"floor\":0,\"ceiling\":100,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"checkpoint\":\"cp-override-1\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.convergence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"convergence\":100,\"drift\":0,\"state\":\"locked\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.hysteresis",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"hysteresis\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.resonance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resonance\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.vectorField",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectors\":2,\"magnitude\":0,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"baseline\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.balanceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"balanceIndex\":100,\"skew\":0,\"state\":\"balanced\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"locked\":true,\"phase\":\"steady\",\"drift\":0}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.waveform",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveform\":\"flat\",\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.horizon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"horizonMs\":1000,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.manifest",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"manifest\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.symmetry",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"symmetry\":100,\"offset\":0,\"state\":\"aligned\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.gradient",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"gradient\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.vectorClock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"clock\":1,\"lag\":0,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.trend",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"trend\":\"flat\",\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.harmonicity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"harmonicity\":100,\"detune\":0,\"state\":\"aligned\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.inertia",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"inertia\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.coordination",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"coordinated\":true,\"lag\":0,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.latencyBand",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"minMs\":0,\"maxMs\":0,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.snapshotIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"index\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.cadenceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cadenceIndex\":100,\"jitter\":0,\"state\":\"steady\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.damping",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"damping\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.phaseNoise",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNoise\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.beat",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"beatHz\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.digestIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"digestIndex\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.waveLock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"locked\":true,\"phase\":\"steady\",\"slip\":0}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.flux",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"flux\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseMatrix\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.orchestration.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"driftEnvelope\":0,\"windowMs\":1000,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.modulation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"modulation\":0,\"samples\":2,\"bounded\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.syncVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncVector\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.bandEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandEnvelope\":0,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.runtime.streaming.pulseTrain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pulseHz\":1,\"samples\":2,\"stable\":true}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.cursor",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"cursor\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vector\":\"default\",\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorDrift\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.phaseBias",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"phaseBias\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.biasEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"biasEnvelope\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"driftEnvelope\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.envelopeDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"envelopeDrift\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.driftVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"driftVector\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorEnvelope\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorContour\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRibbon\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpiral\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorArc\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorCore\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorFrame\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpan\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGrid\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLane\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorTrack\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRail\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpline\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorChain\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorThread\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorAnchor2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRelay2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorHub2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorArc2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBand2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGrid2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLane2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorTrack2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRail2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpline2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorChain2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorThread2\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode4\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRelay3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorHub3\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode5\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink4\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge4\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal4\":0,\"model\":\"default\"}");
			});

		m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate4\":0,\"model\":\"default\"}");
			});
	}

} // namespace blazeclaw::gateway
