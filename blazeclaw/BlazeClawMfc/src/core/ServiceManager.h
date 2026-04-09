#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"
#include "FeatureRegistry.h"
#include "AgentsCatalogService.h"
#include "AgentsAuthProfileService.h"
#include "AgentsModelRoutingService.h"
#include "AgentsSandboxService.h"
#include "AgentsShellRuntimeService.h"
#include "AgentsTranscriptSafetyService.h"
#include "AgentsToolPolicyService.h"
#include "AgentsWorkspaceService.h"
#include "AcpSpawnService.h"
#include "OnnxEmbeddingsService.h"
#include "PiEmbeddedService.h"
#include "RetrievalMemoryService.h"
#include "SubagentRegistryService.h"
#include "HookCatalogService.h"
#include "HookEventService.h"
#include "HookExecutionService.h"
#include "SkillsCommandService.h"
#include "SkillsCatalogService.h"
#include "SkillsEnvOverrideService.h"
#include "SkillsEligibilityService.h"
#include "SkillsInstallService.h"
#include "SkillsPromptService.h"
#include "SkillSecurityScanService.h"
#include "SkillsSyncService.h"
#include "SkillsWatchService.h"
#include "providers/CDeepSeekClient.h"
#include "skills/CSkillsHooksCoordinator.h"
#include "bootstrap/CServiceBootstrapCoordinator.h"
#include "diagnostics/CDiagnosticsReportBuilder.h"
#include "tools/CToolRuntimeRegistry.h"
#include "runtime/CChatRuntime.h"
#include "runtime/ChatRuntimeContracts.h"
#include "runtime/LocalModel/OnnxTextGenerationRuntime.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace blazeclaw::core {

	class ServiceManager {
	public:
		ServiceManager();

		bool Start(const blazeclaw::config::AppConfig& config);
		void Stop();

		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] const FeatureRegistry& Registry() const noexcept;
		[[nodiscard]] const AgentScopeSnapshot& AgentsScope() const noexcept;
		[[nodiscard]] const AgentsWorkspaceSnapshot& AgentsWorkspace() const noexcept;
		[[nodiscard]] const SubagentRegistrySnapshot& SubagentRegistry() const noexcept;
		[[nodiscard]] const AcpSpawnDecision& LastAcpDecision() const noexcept;
		[[nodiscard]] std::size_t ActiveEmbeddedRuns() const noexcept;
		[[nodiscard]] const AgentsToolPolicySnapshot& ToolPolicy() const noexcept;
		[[nodiscard]] std::size_t ShellProcessCount() const noexcept;
		[[nodiscard]] const ModelRoutingSnapshot& ModelRouting() const noexcept;
		[[nodiscard]] const AuthProfileSnapshot& AuthProfiles() const noexcept;
		[[nodiscard]] const SandboxSnapshot& Sandbox() const noexcept;
		[[nodiscard]] const EmbeddingsServiceSnapshot& Embeddings() const noexcept;
		[[nodiscard]] const localmodel::LocalModelRuntimeSnapshot& LocalModelRuntime() const noexcept;
		[[nodiscard]] bool LocalModelRolloutEligible() const noexcept;
		[[nodiscard]] bool LocalModelActivationEnabled() const noexcept;
		[[nodiscard]] const std::string& LocalModelActivationReason() const noexcept;
		[[nodiscard]] const RetrievalMemorySnapshot& RetrievalMemory() const noexcept;
		[[nodiscard]] std::string BuildOperatorDiagnosticsReport() const;
		void SetActiveChatProvider(
			const std::string& provider,
			const std::string& model);
		[[nodiscard]] const std::string& ActiveChatProvider() const noexcept;
		[[nodiscard]] const std::string& ActiveChatModel() const noexcept;
		[[nodiscard]] bool HasDeepSeekCredential() const;
		[[nodiscard]] const SkillsCatalogSnapshot& SkillsCatalog() const noexcept;
		[[nodiscard]] const SkillsEligibilitySnapshot& SkillsEligibility() const noexcept;
		[[nodiscard]] const SkillsPromptSnapshot& SkillsPrompt() const noexcept;
		[[nodiscard]] std::string InvokeGatewayMethod(
			const std::string& method,
			const std::optional<std::string>& paramsJson = std::nullopt) const;
		[[nodiscard]] blazeclaw::gateway::protocol::ResponseFrame RouteGatewayRequest(
			const blazeclaw::gateway::protocol::RequestFrame& request) const;
		bool PumpGatewayNetworkOnce(std::string& error);

	private:
		struct EmailFallbackResolvedPolicy {
			std::wstring profileId;
			std::vector<std::wstring> backends;
			std::wstring onUnavailable;
			std::wstring onAuthError;
			std::wstring onExecError;
			std::uint32_t retryMaxAttempts = 1;
			std::uint32_t retryDelayMs = 0;
			bool requiresApproval = true;
			std::uint32_t approvalTokenTtlMinutes = 60;
		};

		struct ServiceManagerState {
			struct EmbeddedRuntimeState {
				std::vector<std::string> dynamicLoopCanaryProviders;
				std::vector<std::string> dynamicLoopCanarySessions;
				std::uint64_t dynamicLoopPromotionMinRuns = 20;
				double dynamicLoopPromotionMinSuccessRate = 0.95;
				bool lastDynamicLoopEnabled = false;
				bool lastCanaryEligible = false;
				bool lastPromotionReady = false;
				bool lastFallbackUsed = false;
				std::string lastFallbackReason;
				std::uint64_t runSuccessCount = 0;
				std::uint64_t runFailureCount = 0;
				std::uint64_t runTimeoutCount = 0;
				std::uint64_t runCancelledCount = 0;
				std::uint64_t runFallbackCount = 0;
				std::uint64_t taskDeltaTransitionCount = 0;
				std::uint64_t emailFallbackAttemptCount = 0;
				std::uint64_t emailFallbackSuccessCount = 0;
				std::uint64_t emailFallbackFailureCount = 0;
			};

			struct EmailPolicyState {
				std::wstring rolloutMode = L"legacy";
				std::wstring enforceChannel;
				bool rollbackBridgeEnabled = true;
				bool runtimeEnabled = false;
				bool runtimeEnforce = false;
				bool canaryEligible = false;
			};

			struct HooksState {
				bool engineEnabled = true;
				bool fallbackPromptInjection = false;
				bool reminderEnabled = true;
				std::wstring reminderVerbosity = L"normal";
				std::vector<std::wstring> allowedPackages;
				bool strictPolicyEnforcement = false;
				bool governanceReportingEnabled = true;
				std::filesystem::path governanceReportDir;
				std::uint64_t governanceReportsGenerated = 0;
				std::wstring lastGovernanceReportPath;
				bool autoRemediationEnabled = false;
				bool autoRemediationRequiresApproval = true;
				std::wstring autoRemediationApprovalToken;
				std::wstring autoRemediationTenantId = L"default";
				std::filesystem::path autoRemediationPlaybookDir;
				std::uint32_t autoRemediationTokenMaxAgeMinutes = 1440;
				std::uint64_t autoRemediationExecuted = 0;
				std::wstring lastAutoRemediationStatus;
				std::wstring lastAutoRemediationPlaybookPath;
				std::uint64_t autoRemediationTokenRotations = 0;
				bool remediationTelemetryEnabled = true;
				std::filesystem::path remediationTelemetryDir;
				std::wstring lastRemediationTelemetryPath;
				bool remediationAuditEnabled = true;
				std::filesystem::path remediationAuditDir;
				std::wstring lastRemediationAuditPath;
				std::uint32_t remediationSloMaxDriftDetected = 0;
				std::uint32_t remediationSloMaxPolicyBlocked = 0;
				std::wstring remediationSloStatus;
				bool complianceAttestationEnabled = true;
				std::filesystem::path complianceAttestationDir;
				std::wstring lastComplianceAttestationPath;
				bool enterpriseSlaGovernanceEnabled = true;
				std::wstring enterpriseSlaPolicyId = L"default-policy";
				bool crossTenantAttestationAggregationEnabled = true;
				std::filesystem::path crossTenantAttestationAggregationDir;
				std::wstring lastCrossTenantAttestationAggregationPath;
				std::uint64_t crossTenantAttestationAggregationCount = 0;
				std::wstring crossTenantAttestationAggregationStatus;
				bool selfEvolvingHookTriggered = false;
			};

			struct ChatRuntimeState {
				bool asyncQueueEnabled = true;
				std::uint64_t queueWaitTimeoutMs =
					ServiceManager::kChatRuntimeQueueWaitTimeoutMs;
				std::uint64_t executionTimeoutMs =
					ServiceManager::kChatRuntimeExecutionTimeoutMs;
			};

			EmbeddedRuntimeState embeddedRuntime;
			EmailPolicyState emailPolicy;
			HooksState hooks;
			ChatRuntimeState chatRuntime;
		};

		[[nodiscard]] EmailFallbackResolvedPolicy ResolveEmailFallbackPolicy(
			const std::wstring& toolName,
			const std::wstring& capabilityName) const;
		static constexpr std::size_t kChatRuntimeQueueCapacity =
			runtime::contracts::kDefaultQueueCapacity;
		static constexpr std::uint64_t kChatRuntimeQueueWaitTimeoutMs =
			runtime::contracts::kDefaultQueueWaitTimeoutMs;
		static constexpr std::uint64_t kChatRuntimeExecutionTimeoutMs =
			runtime::contracts::kDefaultExecutionTimeoutMs;

		[[nodiscard]] bool IsLocalModelRolloutEligible() const;
		[[nodiscard]] bool IsEmbeddedDynamicLoopCanaryEligible(
			const std::string& provider,
			const std::string& sessionId) const;
		[[nodiscard]] bool IsEmbeddedDynamicLoopPromotionReady() const;
		[[nodiscard]] bool ShouldFallbackFromEmbeddedFailure(
			const std::string& errorCode,
			const std::string& reason) const;

		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState() const;
		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayEntry BuildGatewaySkillEntry(
			const SkillsCatalogEntry& entry,
			const SkillsEligibilityEntry* eligibility,
			const SkillsCommandSpec* command,
			const SkillsInstallPlanEntry* install) const;
		void RefreshSkillsState(
			const blazeclaw::config::AppConfig& config,
			bool forceRefresh,
			const std::wstring& reason);
		void ConfigurePolicies(
			const blazeclaw::config::AppConfig& config);
		void InitializeModules();
		void WireGatewayCallbacks();
		[[nodiscard]] bool FinalizeStartup(
			const blazeclaw::config::AppConfig& config);

		bool m_running = false;
		std::string m_activeChatProvider = "local";
		std::string m_activeChatModel = "default";
		blazeclaw::config::AppConfig m_activeConfig;
		EmailFallbackResolvedPolicy m_emailFallbackResolvedPolicy;
		FeatureRegistry m_registry;
		AgentsCatalogService m_agentsCatalogService;
		AgentScopeSnapshot m_agentsScope;
		AgentsWorkspaceService m_agentsWorkspaceService;
		AgentsWorkspaceSnapshot m_agentsWorkspace;
		AgentsToolPolicyService m_agentsToolPolicyService;
		AgentsToolPolicySnapshot m_agentsToolPolicy;
		AgentsShellRuntimeService m_agentsShellRuntimeService;
		AgentsModelRoutingService m_agentsModelRoutingService;
		ModelRoutingSnapshot m_modelRouting;
		AgentsAuthProfileService m_agentsAuthProfileService;
		AuthProfileSnapshot m_authProfiles;
		AgentsSandboxService m_agentsSandboxService;
		SandboxSnapshot m_sandbox;
		AgentsTranscriptSafetyService m_agentsTranscriptSafetyService;
		SubagentRegistryService m_subagentRegistryService;
		SubagentRegistrySnapshot m_subagentRegistry;
		AcpSpawnService m_acpSpawnService;
		AcpSpawnDecision m_lastAcpDecision;
		OnnxEmbeddingsService m_embeddingsService;
		EmbeddingsServiceSnapshot m_embeddings;
		localmodel::OnnxTextGenerationRuntime m_localModelRuntime;
		localmodel::LocalModelRuntimeSnapshot m_localModelRuntimeSnapshot;
		bool m_localModelRolloutEligible = false;
		bool m_localModelActivationEnabled = false;
		std::string m_localModelActivationReason;
		RetrievalMemoryService m_retrievalMemoryService;
		RetrievalMemorySnapshot m_retrievalMemory;
		PiEmbeddedService m_piEmbeddedService;
		ServiceManagerState m_state;
		HookCatalogService m_hookCatalogService;
		HookCatalogSnapshot m_hookCatalog;
		HookEventService m_hookEventService;
		HookEventSnapshot m_hookEvents;
		HookExecutionService m_hookExecutionService;
		HookExecutionSnapshot m_hookExecution;
		SkillsCatalogService m_skillsCatalogService;
		SkillsCatalogSnapshot m_skillsCatalog;
		SkillsEligibilityService m_skillsEligibilityService;
		SkillsEligibilitySnapshot m_skillsEligibility;
		SkillsPromptService m_skillsPromptService;
		SkillsPromptSnapshot m_skillsPrompt;
		SkillsCommandService m_skillsCommandService;
		SkillsCommandSnapshot m_skillsCommands;
		SkillsSyncService m_skillsSyncService;
		SkillsSyncSnapshot m_skillsSync;
		SkillsEnvOverrideService m_skillsEnvOverrideService;
		SkillsEnvOverrideSnapshot m_skillsEnvOverrides;
		SkillsInstallService m_skillsInstallService;
		SkillsInstallSnapshot m_skillsInstall;
		SkillSecurityScanService m_skillSecurityScanService;
		SkillSecurityScanSnapshot m_skillSecurityScan;
		SkillsWatchService m_skillsWatchService;
		SkillsWatchSnapshot m_skillsWatch;
		CChatRuntime m_chatRuntime;
		CDeepSeekClient m_deepSeekClient;
		CSkillsHooksCoordinator m_skillsHooksCoordinator;
		CServiceBootstrapCoordinator m_serviceBootstrapCoordinator;
		CDiagnosticsReportBuilder m_diagnosticsReportBuilder;
		CToolRuntimeRegistry m_toolRuntimeRegistry;
		blazeclaw::gateway::GatewayHost m_gatewayHost;

		[[nodiscard]] std::optional<std::string> ResolveDeepSeekCredentialUtf8() const;
		[[nodiscard]] blazeclaw::gateway::GatewayHost::ChatRuntimeResult
			InvokeDeepSeekRemoteChat(
				const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
				const std::string& modelId,
				const std::string& apiKey) const;
		[[nodiscard]] bool IsDeepSeekRunCancelled(const std::string& runId) const;
		void MarkDeepSeekRunCancelled(const std::string& runId);
		void ClearDeepSeekRunCancelled(const std::string& runId);
		[[nodiscard]] bool IsEmbeddedRunCancelled(const std::string& runId) const;
		void MarkEmbeddedRunCancelled(const std::string& runId);
		void ClearEmbeddedRunCancelled(const std::string& runId);

		mutable std::mutex m_deepSeekCancelMutex;
		mutable std::unordered_map<std::string, bool> m_deepSeekCancelledRuns;
		mutable std::mutex m_embeddedCancelMutex;
		mutable std::unordered_map<std::string, bool> m_embeddedCancelledRuns;
	};

} // namespace blazeclaw::core
