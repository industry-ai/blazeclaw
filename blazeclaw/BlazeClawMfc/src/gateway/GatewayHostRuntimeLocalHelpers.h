#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "GatewayHost.h"
#include "GatewayToolRegistry.h"
#include "GatewayWebSocketTransport.h"
#include "GatewayEventFanoutService.h"
#include "PluginRuntimeStateService.h"

namespace blazeclaw::gateway {

	namespace runtime_local {

#include "GatewayHostRuntimeLocalHelpers.Types.h"

		// Shared declarations for helpers implemented in GatewayHostRuntimeLocalHelpers.cpp (body was
		// GatewayHost.Handlers.RuntimeHelpers.inl). Template that must instantiate in every TU that calls it:
		inline constexpr std::size_t kMaxChatEventsPerSession = 200;

		template <typename T>
		inline void PushEventWithRetentionLimit(std::deque<T>& queue, T eventState) {
			queue.push_back(std::move(eventState));
			while (queue.size() > kMaxChatEventsPerSession) {
				queue.pop_front();
			}
		}

		std::string SerializeStringArrayLocal(
			const std::vector<std::string>& values);
		std::string EscapeJsonLocal(const std::string& value);
		std::string NormalizeJsonRawForPayload(
			const std::string& raw,
			const char* fallbackRaw);
		std::string SerializeConfigUiHintLocal(
			const blazeclaw::config::ConfigUiHintModel& hint);
		std::string SerializeConfigSchemaChildLocal(
			const ConfigSchemaGatewayChild& child);
		std::string SerializeConfigSchemaLookupResultLocal(
			const ConfigSchemaGatewayLookupResult& result);
		std::optional<std::string> NormalizeSchemaLookupPathRequest(
			const std::string& rawPath);
		std::string ExtractStringParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName);
		std::string NormalizeSearchQueryTextLocal(const std::string& input);
		std::optional<std::string> DeriveCompactSearchQueryLocal(
			const std::string& source);
		std::string SerializePluginRuntimeSubagentModeLocal(
			const PluginRuntimeSubagentMode mode);
		std::string SerializePluginRuntimeCapabilitiesJsonLocal(
			const std::vector<PluginRuntimeCapabilityContract>& contracts);
		std::string SerializePluginRuntimeTransitionsJsonLocal(
			const std::vector<PluginRuntimeTransitionEntry>& transitions,
			const std::size_t limit);
		std::string SerializeSkillCatalogEntry(
			const SkillsCatalogGatewayEntry& entry);
		GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizeTaskDeltaEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionKey,
			const std::size_t defaultIndex);
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> EnsureRuntimeTaskDeltas(
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
			const std::string& runId,
			const std::string& sessionKey,
			const bool success,
			const std::string& assistantText,
			const std::string& errorCode,
			const std::string& errorMessage);
		std::optional<std::size_t> ExtractSizeParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName);
		std::optional<bool> ExtractBoolParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName);
		bool HasAgentId(
			const GatewayAgentRegistry& registry,
			const std::string& agentId);
		bool HasSessionId(
			const GatewaySessionRegistry& registry,
			const std::string& sessionId);
		std::uint64_t CurrentEpochMsLocal();
		std::string BuildAssistantFinalMessageJson(
			const std::string& text,
			const std::uint64_t timestampMs,
			const bool finalTextReplaced = false);
		std::string BuildAssistantDeltaMessageJson(const std::string& text);
		std::string BuildUserMessageJson(
			const std::string& text,
			const bool hasAttachments,
			const std::uint64_t timestampMs);
		std::string BuildChatEventJson(
			const std::string& runId,
			const std::string& sessionKey,
			const std::string& state,
			const std::optional<std::string>& messageJson,
			const std::optional<std::string>& errorCode,
			const std::optional<std::string>& errorMessage,
			const std::optional<std::string>& contextJson,
			const bool approvalRequired,
			const std::optional<std::string>& approvalToken,
			const std::optional<std::uint64_t>& approvalTokenExpiresAtEpochMs,
			const std::optional<std::string>& approvalNextAction,
			const std::optional<std::string>& terminalReason,
			const std::uint64_t timestampMs);
		void EmitPushLifecycleEvent(
			GatewayWebSocketTransport& transport,
			const GatewayEventFanoutService& fanout,
			const GatewayEventFanoutService::ChatLifecycleEvent& event,
			std::uint64_t& eventSeq);
		bool IsTerminalChatState(const std::string& state);
		std::string SerializeTaskDeltaEntryJson(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta);
		std::string ToLowerCopyLocal(const std::string& value);
		std::string Utf8LiteralLocal(const char* value);
		std::string Utf8LiteralLocal(const char8_t* value);
		bool IsLikelyChinesePromptLocal(const std::string& text);
		protocol::ErrorShape BuildRuntimeErrorShape(
			const std::string& code,
			const std::string& message,
			const std::string& runId,
			const std::string& sessionKey);
		std::string ResolveCurrentLocalTimeHHmm();
		std::optional<std::string> TryParsePromptSendAt(
			const std::string& message);
		PromptScheduleResolution ResolvePromptSchedule(
			const std::string& message,
			const std::string& loweredMessage);
		bool HasWeatherIntent(const std::string& loweredMessage);
		bool HasEmailIntent(const std::string& loweredMessage);
		bool HasReportIntent(const std::string& loweredMessage);
		std::string ExtractFirstEmailAddress(const std::string& text);
		std::string ResolvePromptCity(const std::string& message);
		std::string ResolvePromptDate(const std::string& message);
		std::string ResolvePromptSendAt(const std::string& message);
		std::string BuildWeatherReportText(
			const std::string& city,
			const std::string& date,
			const std::string& condition,
			const int temperatureC,
			const std::string& wind,
			const int humidityPct,
			const bool preferChinese);
		void ResolveFallbackProbeDiagnostic(
			const std::string& rawOutput,
			std::string& outCode,
			std::string& outMessage);
		ChatPromptOrchestrationResult TryOrchestrateWeatherEmailPrompt(
			GatewayToolRegistry& toolRegistry,
			const std::string& message);
		bool IsDeepSeekDiagnosticsVerboseEnabled();
		void EmitDeepSeekGatewayDiagnostic(
			const char* stage,
			const std::string& detail,
			const bool verboseOnly = true);
		bool IsSilentReplyText(const std::string& text);
		bool IsSilentAssistantMessageJson(const std::string& messageJson);
		void PushHistoryMessageIfNew(
			std::vector<std::string>& history,
			const std::string& messageJson);
		bool ValidateAttachmentPayloadShape(
			const std::optional<std::string>& paramsJson,
			bool& hasAttachments,
			std::string& errorCode,
			std::string& errorMessage);
		std::vector<std::string> ExtractAttachmentMimeTypes(
			const std::optional<std::string>& paramsJson);
		std::vector<std::string> ParseJsonStringArrayLocal(
			const std::string& rawArray);
		std::string ResolvePreferredToolForNamespace(
			const std::string& normalizedNamespace,
			const std::vector<ToolCatalogEntry>& tools);
		std::string NormalizeOrderedTargetToken(const std::string& token);
		std::vector<std::string> ExtractOrderedTargetsFromPrompt(
			const std::string& message,
			std::vector<std::string>* explicitCallTargets);
		bool HasStructuralSequenceSignal(const std::string& message);
		std::string ResolveOrderedTargetToToolId(
			const std::string& target,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries);
		OrderedSequencePreflight BuildOrderedSequencePreflight(
			const std::string& message,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries);
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			BuildOrderedPreflightTaskDeltas(
				const std::string& runId,
				const std::string& sessionKey,
				const OrderedSequencePreflight& preflight,
				const bool terminalFailure,
				const std::string& terminalErrorCode,
				const std::string& terminalErrorMessage);
		std::string JoinOrderedTargets(const std::vector<std::string>& targets);
		std::string BuildOrderedStepPreflightLabel(const std::string& resolvedTarget);
		bool EndsWithLocal(
			const std::string& value,
			const std::string& suffix);
		bool IsResolvedRuntimeToolTarget(
			const std::string& resolvedToolId,
			const std::vector<ToolCatalogEntry>& runtimeTools);
		bool IsInvalidArgumentsResult(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta);
		std::string ExtractFirstHttpUrl(
			const std::string& text);
		bool TryBuildRecoveredArgsJson(
			const std::string& toolId,
			const std::string& message,
			std::string& outArgsJson);
		std::string SkillNamespaceOfToolId(const std::string& toolId);
		int IntentSimilarityScore(
			const std::string& loweredMessage,
			const ToolCatalogEntry& tool,
			const std::string& referenceCategory,
			const std::string& referenceNamespace);
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			ApplyInvalidArgumentsRecoveryPolicy(
				const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& source,
				const std::string& runId,
				const std::string& sessionKey,
				const std::string& message,
				GatewayToolRegistry& toolRegistry);
		std::string JoinOrderedResolution(
			const OrderedSequencePreflight& preflight);
		std::string SerializeFloatArrayLocal(
			const std::vector<float>& values);
		std::string SerializeFloatMatrixLocal(
			const std::vector<std::vector<float>>& vectors);

	} // namespace runtime_local

} // namespace blazeclaw::gateway
