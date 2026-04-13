#pragma once

#include "../config/ConfigModels.h"
#include "GatewayAgentRegistry.h"
#include "GatewayChannelRegistry.h"
#include "GatewayMethodDispatcher.h"
#include "GatewaySessionRegistry.h"
#include "GatewayToolRegistry.h"
#include "ExtensionLifecycleManager.h"
#include "ApprovalTokenStore.h"
#include "GatewayWebSocketTransport.h"
#include "GatewayProtocolContract.h"
#include "PluginRuntimeStateService.h"
#include "ChatRunPipelineOrchestrator.h"
#include "TaskDeltaRepository.h"
#include "IGatewayHostRuntime.h"
#include "GatewayHostRouter.h"
#include "GatewayRequestPolicyGuard.h"
#include "GatewayEventFanoutService.h"
#include "TransportRecipientRegistry.h"

#include <memory>

#include <unordered_set>

namespace blazeclaw::gateway {

	struct SkillsCatalogGatewayEntry {
		std::string name;
		std::string skillKey;
		std::string commandName;
		std::string commandToolName;
		std::string commandArgMode;
		std::string commandArgSchema;
		std::string commandResultSchema;
		std::string commandIdempotencyHint;
		std::string commandRetryPolicyHint;
		bool commandRequiresApproval = false;
		std::string installKind;
		std::string installCommand;
		bool installExecutable = false;
		std::string installReason;
		std::string description;
		std::string source;
		std::int32_t precedence = 0;
		bool eligible = false;
		bool disabled = false;
		bool blockedByAllowlist = false;
		bool disableModelInvocation = false;
		bool validFrontmatter = false;
		std::size_t validationErrorCount = 0;
		std::string primaryEnv;
		std::vector<std::string> requiresBins;
		std::vector<std::string> requiresEnv;
		std::vector<std::string> requiresConfig;
		std::vector<std::string> configPathHints;
		std::string pluginConfigSchemaJson;
		std::string pluginConfigUiHintsJson;
		std::string channelConfigSchemasJson;
		std::string channelConfigUiHintsJson;
		std::vector<std::string> normalizedMetadataSources;
		std::vector<std::string> missingEnv;
		std::vector<std::string> missingConfig;
		std::vector<std::string> missingBins;
		std::vector<std::string> missingAnyBins;
	};

	struct SkillsCatalogGatewayState {
		std::vector<SkillsCatalogGatewayEntry> entries;
		std::size_t rootsScanned = 0;
		std::size_t rootsSkipped = 0;
		std::size_t pluginRootsConfigured = 0;
		std::size_t pluginRootsScanned = 0;
		std::size_t oversizedSkillFiles = 0;
		std::size_t invalidFrontmatterFiles = 0;
		std::size_t warningCount = 0;
		std::size_t eligibleCount = 0;
		std::size_t disabledCount = 0;
		std::size_t blockedByAllowlistCount = 0;
		std::size_t missingRequirementsCount = 0;
		std::size_t promptIncludedCount = 0;
		std::size_t promptChars = 0;
		bool promptTruncated = false;
		std::uint64_t snapshotVersion = 0;
		bool watchEnabled = true;
		std::uint32_t watchDebounceMs = 250;
		std::string watchReason;
		std::string prompt;
		bool sandboxSyncOk = false;
		std::string sandboxDestinationNamingMode;
		std::size_t sandboxDestinationCollisions = 0;
		std::size_t sandboxSourceDirFallbacks = 0;
		std::size_t sandboxSynced = 0;
		std::size_t sandboxSkipped = 0;
		std::size_t envAllowed = 0;
		std::size_t envBlocked = 0;
		std::size_t installExecutableCount = 0;
		std::size_t installBlockedCount = 0;
		std::size_t scanInfoCount = 0;
		std::size_t scanWarnCount = 0;
		std::size_t scanCriticalCount = 0;
		std::size_t scanScannedFiles = 0;
		bool governanceReportingEnabled = false;
		std::size_t governanceReportsGenerated = 0;
		std::string lastGovernanceReportPath;
		std::size_t policyBlockedCount = 0;
		std::size_t driftDetectedCount = 0;
		std::string lastDriftReason;
		bool autoRemediationEnabled = false;
		bool autoRemediationRequiresApproval = true;
		std::size_t autoRemediationExecuted = 0;
		std::string lastAutoRemediationStatus;
		std::string autoRemediationTenantId;
		std::string lastAutoRemediationPlaybookPath;
		std::size_t autoRemediationTokenMaxAgeMinutes = 0;
		std::size_t autoRemediationTokenRotations = 0;
		std::string lastRemediationTelemetryPath;
		std::string lastRemediationAuditPath;
		std::string remediationSloStatus;
		std::size_t remediationSloMaxDriftDetected = 0;
		std::size_t remediationSloMaxPolicyBlocked = 0;
		std::string lastComplianceAttestationPath;
		std::string enterpriseSlaPolicyId;
		bool crossTenantAttestationAggregationEnabled = false;
		std::string crossTenantAttestationAggregationStatus;
		std::size_t crossTenantAttestationAggregationCount = 0;
		std::string lastCrossTenantAttestationAggregationPath;
	};

	struct ConfigSchemaGatewayChild {
		std::string key;
		std::string path;
		std::string type;
		bool required = false;
		bool hasChildren = false;
		std::optional<blazeclaw::config::ConfigUiHintModel> hint;
		std::string hintPath;
	};

	struct ConfigSchemaGatewayLookupResult {
		std::string path;
		std::string schemaJson;
		std::optional<blazeclaw::config::ConfigUiHintModel> hint;
		std::string hintPath;
		std::vector<ConfigSchemaGatewayChild> children;
	};

	struct ConfigSchemaGatewayState {
		std::string schemaJson;
		std::string uiHintsJson;
		std::string version;
		std::string generatedAt;
	};

	class GatewayHost : public IGatewayHostRuntime {
	public:
		using SkillsRefreshCallback = std::function<SkillsCatalogGatewayState()>;
		using SkillsUpdateCallback = std::function<protocol::ResponseFrame(
			const protocol::RequestFrame& request)>;
		using ConfigSchemaGetCallback = std::function<ConfigSchemaGatewayState()>;
		using ConfigSchemaLookupCallback = std::function<
			std::optional<ConfigSchemaGatewayLookupResult>(const std::string& path)>;

		struct ChatRuntimeRequest {
			std::string runId;
			std::string sessionKey;
			std::string message;
			bool enforceOrderedAllowlist = false;
			std::vector<std::string> orderedAllowedToolTargets;
			bool hasAttachments = false;
			std::vector<std::string> attachmentMimeTypes;
			std::function<void(const std::string&)> onAssistantDelta;
		};

		struct ChatAbortRequest {
			std::string runId;
			std::string sessionKey;
		};

		struct ChatRuntimeResult {
			using TaskDeltaEntry = blazeclaw::gateway::TaskDeltaEntry;

			bool ok = false;
			std::string assistantText;
			std::vector<std::string> assistantDeltas;
			std::vector<TaskDeltaEntry> taskDeltas;
			std::string modelId;
			std::string errorCode;
			std::string errorMessage;
		};

		struct EmbeddingsGenerateRequest {
			std::string text;
			std::optional<bool> normalize;
			std::string model;
			std::string traceId;
		};

		struct EmbeddingsGenerateResult {
			bool ok = false;
			std::vector<float> vector;
			std::size_t dimension = 0;
			std::string provider;
			std::string modelId;
			std::uint32_t latencyMs = 0;
			std::string status;
			std::string errorCode;
			std::string errorMessage;
		};

		struct EmbeddingsBatchRequest {
			std::vector<std::string> texts;
			std::optional<bool> normalize;
			std::string model;
			std::string traceId;
		};

		struct EmbeddingsBatchResult {
			bool ok = false;
			std::vector<std::vector<float>> vectors;
			std::size_t dimension = 0;
			std::string provider;
			std::string modelId;
			std::uint32_t latencyMs = 0;
			std::string status;
			std::string errorCode;
			std::string errorMessage;
		};

		using ChatRuntimeCallback = std::function<ChatRuntimeResult(const ChatRuntimeRequest&)>;
		using ChatAbortCallback = std::function<bool(const ChatAbortRequest&)>;
		using EmbeddingsGenerateCallback =
			std::function<EmbeddingsGenerateResult(const EmbeddingsGenerateRequest&)>;
		using EmbeddingsBatchCallback =
			std::function<EmbeddingsBatchResult(const EmbeddingsBatchRequest&)>;

		bool Start(const blazeclaw::config::GatewayConfig& config);
		bool StartLocalOnly(const blazeclaw::config::GatewayConfig& config);
		bool StartLocalDispatchOnly();
		bool StartLocalRuntimeDispatchOnly();
		bool BootstrapCreateRuntimeState(
			const blazeclaw::config::GatewayConfig& config);
		bool BootstrapStartRuntimeServices();
		bool BootstrapAttachTransportHandlers();
		bool BootstrapStartRuntimeSubscriptions();
		bool BootstrapFinalizeRuntimeInitialization();
		void Stop();
		void SetSkillsCatalogState(SkillsCatalogGatewayState state);
		void SetSkillsRefreshCallback(SkillsRefreshCallback callback);
		void SetSkillsUpdateCallback(SkillsUpdateCallback callback);
		void SetConfigSchemaGetCallback(ConfigSchemaGetCallback callback);
		void SetConfigSchemaLookupCallback(ConfigSchemaLookupCallback callback);
		void SetEmbeddedOrchestrationPath(const std::string& path);
		void SetEmailFallbackRuntimeFlags(
			bool preflightEnabled,
			bool policyProfilesEnabled,
			bool policyProfilesEnforce);
		void SetEmailFallbackResolvedPolicy(
			const std::vector<std::string>& backends,
			const std::string& onUnavailable,
			const std::string& onAuthError,
			const std::string& onExecError,
			std::uint32_t retryMaxAttempts,
			std::uint32_t retryDelayMs,
			bool requiresApproval,
			std::uint32_t approvalTokenTtlMinutes,
			const std::string& profileId);
		void SetChatRuntimeCallback(ChatRuntimeCallback callback);
		void SetChatAbortCallback(ChatAbortCallback callback);
		void SetEmbeddingsGenerateCallback(EmbeddingsGenerateCallback callback);
		void SetEmbeddingsBatchCallback(EmbeddingsBatchCallback callback);

		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] std::string LastWarning() const;
		[[nodiscard]] std::string HandleInboundText(const std::string& inboundJson) const;
		bool AcceptConnection(const std::string& connectionId, std::string& error);
		bool PumpInboundFrame(const std::string& connectionId, const std::string& inboundFrame, std::string& error);
		std::vector<std::string> DrainOutboundFrames(const std::string& connectionId, std::string& error);
		bool PumpNetworkOnce(std::string& error);
		[[nodiscard]] std::string BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildHealthEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::vector<ToolCatalogEntry> ListRuntimeTools() const;
		[[nodiscard]] ToolExecuteResult ExecuteRuntimeTool(
			const std::string& tool,
			const std::optional<std::string>& argsJson = std::nullopt);
		[[nodiscard]] ToolExecuteResultV2 ExecuteRuntimeToolV2(
			const ToolExecuteRequestV2& request);
		void RegisterRuntimeTool(
			const ToolCatalogEntry& tool,
			GatewayToolRegistry::RuntimeToolExecutor executor);
		void RegisterRuntimeToolV2(
			const ToolCatalogEntry& tool,
			GatewayToolRegistry::RuntimeToolExecutorV2 executor);

		[[nodiscard]] protocol::ResponseFrame RouteRequest(const protocol::RequestFrame& request) const override;
		[[nodiscard]] bool IsHealthy() const noexcept override;

	private:
		friend class GatewayHostEx;

		[[nodiscard]] protocol::ResponseFrame RouteRequestLegacy(
			const protocol::RequestFrame& request) const;

		struct AgentRunState {
			std::string runId;
			std::string agentId;
			std::string sessionId;
			std::string message;
			std::string status;
			std::string summary;
			std::uint64_t startedAtMs = 0;
			std::optional<std::uint64_t> completedAtMs;
		};

		struct ChatRunState {
			std::string runId;
			std::string sessionKey;
			std::string idempotencyKey;
			std::string userMessage;
			std::string assistantText;
			std::vector<std::string> providerDeltas;
			std::size_t providerDeltaCursor = 0;
			std::size_t streamCursor = 0;
			std::uint64_t lastEmitMs = 0;
			bool failed = false;
			std::string errorMessage;
			std::uint64_t startedAtMs = 0;
			bool active = true;
			bool terminalEventEnqueued = false;
			bool pushLifecycleRequested = false;
			bool toolEventsAllowed = false;
			std::string originatingChannel = "internal";
			std::string originatingTo;
			bool explicitDeliverRoute = false;
		};

		struct ChatEventState {
			std::string runId;
			std::string sessionKey;
			std::string state;
			std::optional<std::string> messageJson;
			std::optional<std::string> errorMessage;
			std::uint64_t timestampMs = 0;
		};

		struct ChatReplayEntry {
			bool ok = false;
			std::optional<std::string> payloadJson;
			std::optional<protocol::ErrorShape> error;
		};

		void RegisterDefaultHandlers();
		void RegisterChannelsHandlers();
		void RegisterEventHandlers();
		void RegisterToolsHandlers();
		void RegisterScopeClusterHandlers();
		void RegisterGeneratedScopeClusterHandlers();
		void RegisterSecurityOpsHandlers();
		void RegisterRuntimeHandlers();
		void RegisterTransportHandlers();
		[[nodiscard]] bool CreateRuntimeState(
			const blazeclaw::config::GatewayConfig& config);
		[[nodiscard]] bool StartRuntimeServices();
		[[nodiscard]] bool AttachTransportRuntime();
		[[nodiscard]] bool StartRuntimeSubscriptions();
		[[nodiscard]] bool FinalizeRuntimeInitialization();
		void LoadPersistedTaskDeltas();
		void PersistTaskDeltas() const;
		bool InitializeRuntime(const blazeclaw::config::GatewayConfig& config);
		void EnsureFixtureParityValidated();

		bool m_running = false;
		bool m_initialized = false;
		bool m_dispatchInitialized = false;
		bool m_runtimeHandlersInitialized = false;
		std::string m_bindAddress;
		std::uint16_t m_port = 0;
		std::string m_runtimeGatewayBind = "127.0.0.1";
		std::uint16_t m_runtimeGatewayPort = 18789;
		std::string m_runtimeAgentModel = "default";
		bool m_runtimeAgentStreaming = true;
		std::string m_runtimeDeepSeekApiKey;
		std::string m_runtimeDeepSeekBaseUrl = "https://api.deepseek.com";
		std::string m_runtimeDeepSeekDefaultModel = "deepseek/deepseek-chat";
		bool m_runtimeEmailPreflightEnabled = false;
		bool m_runtimeEmailPolicyProfilesEnabled = false;
		bool m_runtimeEmailPolicyProfilesEnforce = false;
		std::vector<std::string> m_runtimeEmailResolvedBackends;
		std::string m_runtimeEmailPolicyOnUnavailable = "continue";
		std::string m_runtimeEmailPolicyOnAuthError = "stop";
		std::string m_runtimeEmailPolicyOnExecError = "retry_then_continue";
		std::uint32_t m_runtimeEmailRetryMaxAttempts = 1;
		std::uint32_t m_runtimeEmailRetryDelayMs = 0;
		bool m_runtimeEmailRequiresApproval = true;
		std::uint32_t m_runtimeEmailApprovalTokenTtlMinutes = 60;
		std::string m_runtimeEmailPolicyProfileId = "default";
		std::string m_embeddedOrchestrationPath = "dynamic_task_delta";
		std::string m_runtimeAssignedSessionId = "main";
		std::string m_runtimeAssignedAgentId = "default";
		std::size_t m_runtimeQueueDepth = 0;
		std::size_t m_runtimeRunningCount = 0;
		std::size_t m_runtimeQueueCapacity = 8;
		std::size_t m_runtimeAssignmentCount = 0;
		std::size_t m_runtimeRebalanceCount = 0;
		std::size_t m_runtimeDrainCount = 0;
		std::size_t m_streamingBufferedFrames = 0;
		std::size_t m_streamingBufferedBytes = 0;
		std::size_t m_streamingHighWatermark = 16;
		std::size_t m_streamingWindowMs = 5000;
		std::size_t m_streamingThrottleLimitPerSec = 120;
		bool m_streamingThrottled = false;
		bool m_failoverOverrideActive = false;
		std::string m_failoverOverrideModel = "default";
		std::string m_failoverOverrideReason = "none";
		std::size_t m_failoverOverrideChanges = 0;
		std::size_t m_failoverAttempts = 0;
		std::size_t m_failoverFallbackHits = 0;
		bool m_stagePipelineFeatureEnabled = true;
		std::string m_stagePipelineRolloutCohort = "default";
		std::string m_lastWarning;
		bool m_fixtureParityValidated = false;
		GatewayMethodDispatcher m_dispatcher;
		GatewayWebSocketTransport m_transport;
		GatewayAgentRegistry m_agentRegistry;
		GatewayChannelRegistry m_channelRegistry;
		GatewaySessionRegistry m_sessionRegistry;
		GatewayToolRegistry m_toolRegistry;
		ExtensionLifecycleManager m_extensionLifecycle{ true };
		PluginRuntimeStateService m_pluginRuntimeState;
		ApprovalTokenStore m_approvalStore;
		std::unordered_map<std::string, AgentRunState> m_agentRuns;
		std::unordered_map<std::string, std::string> m_agentRunByIdempotency;
		std::unordered_map<std::string, std::string> m_mutationPayloadByIdempotency;
		std::unordered_map<std::string, std::vector<std::string>> m_chatHistoryBySession;
		std::unordered_map<std::string, std::deque<ChatEventState>> m_chatEventsBySession;
		std::unordered_map<std::string, ChatRunState> m_chatRunsById;
		std::unordered_map<std::string, std::string> m_chatRunByIdempotency;
		std::unordered_map<std::string, ChatReplayEntry> m_chatReplayByIdempotency;
		std::unordered_map<std::string, std::unordered_set<std::string>> m_chatToolEventRecipientsByRun;
		TransportRecipientRegistry m_transportRecipientRegistry;
		std::unordered_set<std::string> m_chatTerminalDeliveredRunIds;
		std::uint64_t m_chatPushEventSeq = 0;
		std::unordered_map<std::string, std::vector<ChatRuntimeResult::TaskDeltaEntry>> m_taskDeltasByRunId;
		std::size_t m_taskDeltasRetentionLimit = 64;
		std::size_t m_taskDeltasMaxPayloadBytes = 1024 * 1024;
		std::uint64_t m_taskDeltaRunSuccessCount = 0;
		std::uint64_t m_taskDeltaRunFailureCount = 0;
		std::uint64_t m_taskDeltaRunTimeoutCount = 0;
		std::uint64_t m_taskDeltaRunCancelledCount = 0;
		std::uint64_t m_taskDeltaRunFallbackCount = 0;
		SkillsCatalogGatewayState m_skillsCatalogState;
		SkillsRefreshCallback m_skillsRefreshCallback;
		SkillsUpdateCallback m_skillsUpdateCallback;
		ConfigSchemaGetCallback m_configSchemaGetCallback;
		ConfigSchemaLookupCallback m_configSchemaLookupCallback;
		ChatRuntimeCallback m_chatRuntimeCallback;
		ChatAbortCallback m_chatAbortCallback;
		EmbeddingsGenerateCallback m_embeddingsGenerateCallback;
		EmbeddingsBatchCallback m_embeddingsBatchCallback;
		ChatRunPipelineOrchestrator m_chatRunPipelineOrchestrator;
		TaskDeltaRepository m_taskDeltaRepository{ m_taskDeltasByRunId };
		GatewayHostRouter m_hostRouter;
		GatewayRequestPolicyGuard m_requestPolicyGuard;
		GatewayEventFanoutService m_eventFanoutService;
		mutable std::unique_ptr<IGatewayHostRuntime> m_stageRuntimeHost;
	};

} // namespace blazeclaw::gateway
