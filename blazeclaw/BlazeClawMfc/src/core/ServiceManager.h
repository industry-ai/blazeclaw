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
#include "runtime/LocalModel/OnnxTextGenerationRuntime.h"
#include <atomic>
#include <mutex>
#include <optional>
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
		[[nodiscard]] bool IsLocalModelRolloutEligible() const;
		[[nodiscard]] bool IsEmbeddedDynamicLoopCanaryEligible(
			const std::string& provider,
			const std::string& sessionId) const;
		[[nodiscard]] bool ShouldFallbackFromEmbeddedFailure(
			const std::string& errorCode,
			const std::string& reason) const;

		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState() const;
		void RefreshSkillsState(
			const blazeclaw::config::AppConfig& config,
			bool forceRefresh,
			const std::wstring& reason);

		bool m_running = false;
		std::string m_activeChatProvider = "local";
		std::string m_activeChatModel = "default";
		blazeclaw::config::AppConfig m_activeConfig;
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
		std::vector<std::string> m_embeddedDynamicLoopCanaryProviders;
		std::vector<std::string> m_embeddedDynamicLoopCanarySessions;
		bool m_lastEmbeddedDynamicLoopEnabled = false;
		bool m_lastEmbeddedCanaryEligible = false;
		bool m_lastEmbeddedFallbackUsed = false;
		std::string m_lastEmbeddedFallbackReason;
		HookCatalogService m_hookCatalogService;
		HookCatalogSnapshot m_hookCatalog;
		HookEventService m_hookEventService;
		HookEventSnapshot m_hookEvents;
		HookExecutionService m_hookExecutionService;
		HookExecutionSnapshot m_hookExecution;
		bool m_hooksEngineEnabled = true;
		bool m_hooksFallbackPromptInjection = false;
		bool m_hooksReminderEnabled = true;
		std::wstring m_hooksReminderVerbosity = L"normal";
		std::vector<std::wstring> m_hooksAllowedPackages;
		bool m_hooksStrictPolicyEnforcement = false;
		bool m_hooksGovernanceReportingEnabled = true;
		std::filesystem::path m_hooksGovernanceReportDir;
		std::uint64_t m_hooksGovernanceReportsGenerated = 0;
		std::wstring m_hooksLastGovernanceReportPath;
		bool m_hooksAutoRemediationEnabled = false;
		bool m_hooksAutoRemediationRequiresApproval = true;
		std::wstring m_hooksAutoRemediationApprovalToken;
		std::wstring m_hooksAutoRemediationTenantId = L"default";
		std::filesystem::path m_hooksAutoRemediationPlaybookDir;
		std::uint32_t m_hooksAutoRemediationTokenMaxAgeMinutes = 1440;
		std::uint64_t m_hooksAutoRemediationExecuted = 0;
		std::wstring m_hooksLastAutoRemediationStatus;
		std::wstring m_hooksLastAutoRemediationPlaybookPath;
		std::uint64_t m_hooksAutoRemediationTokenRotations = 0;
		bool m_hooksRemediationTelemetryEnabled = true;
		std::filesystem::path m_hooksRemediationTelemetryDir;
		std::wstring m_hooksLastRemediationTelemetryPath;
		bool m_hooksRemediationAuditEnabled = true;
		std::filesystem::path m_hooksRemediationAuditDir;
		std::wstring m_hooksLastRemediationAuditPath;
		std::uint32_t m_hooksRemediationSloMaxDriftDetected = 0;
		std::uint32_t m_hooksRemediationSloMaxPolicyBlocked = 0;
		std::wstring m_hooksRemediationSloStatus;
		bool m_hooksComplianceAttestationEnabled = true;
		std::filesystem::path m_hooksComplianceAttestationDir;
		std::wstring m_hooksLastComplianceAttestationPath;
		bool m_hooksEnterpriseSlaGovernanceEnabled = true;
		std::wstring m_hooksEnterpriseSlaPolicyId = L"default-policy";
		bool m_hooksCrossTenantAttestationAggregationEnabled = true;
		std::filesystem::path m_hooksCrossTenantAttestationAggregationDir;
		std::wstring m_hooksLastCrossTenantAttestationAggregationPath;
		std::uint64_t m_hooksCrossTenantAttestationAggregationCount = 0;
		std::wstring m_hooksCrossTenantAttestationAggregationStatus;
		bool m_selfEvolvingHookTriggered = false;
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
		blazeclaw::gateway::GatewayHost m_gatewayHost;

		[[nodiscard]] std::optional<std::string> ResolveDeepSeekCredentialUtf8() const;
		[[nodiscard]] blazeclaw::gateway::GatewayHost::ChatRuntimeResult
			InvokeDeepSeekRemoteChat(
				const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
				const std::string& modelId,
				const std::string& apiKey) const;
		[[nodiscard]] std::optional<std::string> ExtractDeepSeekAssistantText(
			const std::string& responseJson) const;
		[[nodiscard]] std::vector<std::string> ExtractDeepSeekAssistantDeltas(
			const std::string& responseBody) const;
		[[nodiscard]] std::optional<std::string> ExtractDeepSeekErrorMessage(
			const std::string& responseJson) const;
		[[nodiscard]] bool IsDeepSeekRunCancelled(const std::string& runId) const;
		void MarkDeepSeekRunCancelled(const std::string& runId);
		void ClearDeepSeekRunCancelled(const std::string& runId);

		mutable std::mutex m_deepSeekCancelMutex;
		mutable std::unordered_map<std::string, bool> m_deepSeekCancelledRuns;
	};

} // namespace blazeclaw::core
