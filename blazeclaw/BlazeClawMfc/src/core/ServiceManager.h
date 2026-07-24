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
#include "ChatRuntimeOrchestrationCoordinator.h"
#include "EmailFallbackRuntimeCoordinator.h"
#include "EmailPreflightHealthService.h"
#include "EmailPolicyOrchestrationService.h"
#include "EmailRuntimeDiagnosticsProjector.h"
#include "ExtensionBundleCommandSourceAdapter.h"
#include "GatewayLifecycleDiagnosticsProjector.h"
#include "EmbeddedRuntimeDiagnosticsProjector.h"
#include "HooksDiagnosticsProjector.h"
#include "ModelRuntimeDiagnosticsProjector.h"
#include "ChatProviderRuntimeService.h"
#include "OperatorDiagnosticsAssembler.h"
#include "FixtureStartupValidatorFacade.h"
#include "HooksStartupCoordinator.h"
#include "ManagedRuntimeConfigDiffCoordinator.h"
#include "OnnxEmbeddingsService.h"
#include "PiEmbeddedService.h"
#include "RetrievalMemoryService.h"
#include "SpeechTranscriptionCoordinator.h"
#include "runtime/SpeechRecognition/SpeechRecognitionRuntime.h"
#include "runtime/TextToSpeech/ITextToSpeechRuntime.h"
#include "SkillsStartupCoordinator.h"
#include "SubagentRegistryService.h"
#include "HookCatalogService.h"
#include "HookEventService.h"
#include "HookExecutionService.h"
#include "SkillsCommandService.h"
#include "SkillCommandsAggregationService.h"
#include "SkillCommandInvocationService.h"
#include "SkillsCatalogService.h"
#include "SkillsEnvOverrideService.h"
#include "SkillsGatewayMethodHandler.h"
#include "SkillsGatewayProjectionService.h"
#include "SkillsEligibilityService.h"
#include "SkillsFacade.h"
#include "SkillsInstallService.h"
#include "SkillsPromptService.h"
#include "InlineActionsOrchestrationService.h"
#include "SkillSecurityScanService.h"
#include "SkillsSyncService.h"
#include "SkillsWatchService.h"
#include "providers/CDeepSeekClient.h"
#include "skills/CSkillsHooksCoordinator.h"
#include "bootstrap/CServiceBootstrapCoordinator.h"
#include "bootstrap/GatewayRuntimeBootstrapCoordinator.h"
#include "bootstrap/GatewayManagedConfigReloader.h"
#include "diagnostics/CDiagnosticsReportBuilder.h"
#include "ConfigSchemaService.h"
#include "tools/CToolRuntimeRegistry.h"
#include "runtime/CChatRuntime.h"
#include "runtime/ChatRuntimeContracts.h"
#include "runtime/LocalModel/ITextGenerationRuntime.h"
#include "runtime/LocalModel/OnnxTextGenerationRuntime.h"
#include "extensions/RuntimeCapabilityAdapterContracts.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

	class ServiceManager {
	public:
		struct SkillsHostCallbacks {
			std::function<bool(
				const std::string& skill,
				const std::string& envContent,
				std::string& outError,
				std::filesystem::path& outPath)> persistSkillConfigEnv;
			std::function<void()> refreshSkillView;
		};

		// Lifecycle and top-level orchestration
		ServiceManager();
		~ServiceManager();
		void SetSkillsHostCallbacks(SkillsHostCallbacks callbacks);

		bool Start(const blazeclaw::config::AppConfig& config);
		void Stop();

		[[nodiscard]] bool IsRunning() const noexcept;

	private:
		friend class GatewayHostBindingCoordinator;
		friend class ChatRuntimeOrchestrationCoordinator;
		friend class SkillsGatewayPublicationCoordinator;
		friend class ServiceLifecycleStartupCoordinator;

		// Private lifecycle phases:
		void ConfigurePolicies(
			const blazeclaw::config::AppConfig& config);
		void InitializeModules();
		void WireGatewayCallbacks();
		[[nodiscard]] bool FinalizeStartup(
			const blazeclaw::config::AppConfig& config);

	public:
		// Read facade / snapshots / state accessors
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
		[[nodiscard]] const speechrecognition::SpeechRecognitionRuntimeSnapshot& SpeechRecognition() const noexcept;
		[[nodiscard]] const localmodel::LocalModelRuntimeSnapshot& LocalModelRuntime() const noexcept;
		[[nodiscard]] bool LocalModelRolloutEligible() const noexcept;
		[[nodiscard]] bool LocalModelActivationEnabled() const noexcept;
		[[nodiscard]] const std::string& LocalModelActivationReason() const noexcept;
		[[nodiscard]] const RetrievalMemorySnapshot& RetrievalMemory() const noexcept;
		[[nodiscard]] const SkillsCatalogSnapshot& SkillsCatalog() const noexcept;
		[[nodiscard]] const SkillsEligibilitySnapshot& SkillsEligibility() const noexcept;
		[[nodiscard]] const SkillsPromptSnapshot& SkillsPrompt() const noexcept;
		[[nodiscard]] const SkillsRunSnapshot& RunSkillsSnapshot() const noexcept;

	// Text-to-Speech facade
	[[nodiscard]] bool TextToSpeechEnabled() const noexcept;
	std::string StartTextToSpeech(
		const std::string& text,
		const std::string& provider,
		const std::string& model,
		const std::string& voice,
		const std::string& runId);
	void StopTextToSpeech(const std::string& utteranceId);
	[[nodiscard]] texttospeech::TextToSpeechRuntimeSnapshot CollectTextToSpeechSnapshot() const noexcept;

		// Gateway integration surface
		[[nodiscard]] std::string InvokeGatewayMethod(
			const std::string& method,
			const std::optional<std::string>& paramsJson = std::nullopt) const;
		[[nodiscard]] blazeclaw::gateway::protocol::ResponseFrame RouteGatewayRequest(
			const blazeclaw::gateway::protocol::RequestFrame& request) const;
		bool PumpGatewayNetworkOnce(std::string& error);

	private:
		// Private gateway / config schema composition (sequenced by `GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks`).
		// Policy + embeddings `Set*` wiring lives in `GatewayHostBindingCoordinator.cpp` (Phase C); tool runtime
		// registration stays here (TU-local helpers in `ServiceManager.cpp`).
		void BindToolRuntimeCallbacks();
		[[nodiscard]] blazeclaw::gateway::ConfigSchemaGatewayState
			BuildConfigSchemaGatewayState() const;
		[[nodiscard]] std::optional<blazeclaw::gateway::ConfigSchemaGatewayLookupResult>
			LookupConfigSchemaGatewayPath(const std::string& path) const;
		[[nodiscard]] bool WriteConfigSchemaDocumentationSnapshot(
			const std::filesystem::path& outputPath,
			std::wstring& outError) const;

	private:
		// Skills/hooks orchestration and projection
		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState() const;
		void RefreshGatewaySkillsStateProjection();
		void PublishGatewaySkillsStateProjection();
		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayEntry BuildGatewaySkillEntry(
			const SkillsCatalogEntry& entry,
			const SkillsEligibilityEntry* eligibility,
			const SkillsCommandSpec* command,
			const SkillsInstallPlanEntry* install) const;
		[[nodiscard]] std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>
			BuildRuntimeSkillCommandSourceAdapters();
		void RefreshSkillsState(
			const blazeclaw::config::AppConfig& config,
			bool forceRefresh,
			const std::wstring& reason);

	private:
		// Chat runtime orchestration helpers
		[[nodiscard]] std::optional<std::string>
			ResolveSkillInvocationToolTarget(
				const std::string& commandBodyNormalized) const;
		[[nodiscard]] std::optional<std::string>
			ResolveSkillInvocationPromptRewrite(
				const std::string& commandBodyNormalized) const;
		[[nodiscard]] std::optional<std::string>
			ResolveSkillInvocationMissReason(
				const std::string& commandBodyNormalized,
				const std::optional<std::string>& resolvedToolTarget) const;
		[[nodiscard]] std::optional<std::string>
			BuildGeneratedOpenClawRoutingDecisionTelemetry(
				const std::string& commandBodyNormalized) const;
		[[nodiscard]] bool ShouldLoadSkillCommandsForInlineActions(
			bool allowTextCommands,
			const std::string& commandBodyNormalized) const;
		[[nodiscard]] std::vector<std::string>
			BuildOrderedAllowedToolTargets(
				const std::vector<std::string>& requestedTargets,
				const std::optional<std::string>& resolvedTarget) const;
		[[nodiscard]] std::optional<std::string>
			ExtractInlineToolResultText(
				const blazeclaw::gateway::ToolExecuteResultV2& result) const;
		[[nodiscard]] std::optional<blazeclaw::gateway::GatewayHost::ChatRuntimeResult>
			TryExecuteInlineToolInvocation(
				const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
				const std::string& activeModel,
				const std::optional<std::string>& resolvedSkillInvocationToolTarget);
		[[nodiscard]] std::vector<
			blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			ConvertEmbeddedTaskDeltas(
				const std::vector<EmbeddedTaskDelta>& taskDeltas) const;
		void ApplyEmbeddedExecutionTelemetry(
			const EmbeddedRuntimeExecutionResult& embeddedExecution);
		[[nodiscard]] bool IsEmbeddedDynamicLoopCanaryEligible(
			const std::string& provider,
			const std::string& sessionId) const;
		[[nodiscard]] bool IsEmbeddedDynamicLoopPromotionReady() const;
		[[nodiscard]] bool IsLocalModelRolloutEligible() const;

	private:
		// Managed reload / cleanup / lifecycle internals
		[[nodiscard]] bool ApplyManagedRuntimeConfigDiff(
			const blazeclaw::config::AppConfig& nextConfig,
			std::wstring& warningMessage);
		void ApplyManagedRuntimeAuthReject(
			const ManagedRuntimeConfigDiffCoordinator::AuthGuardResult& authGuard,
			std::wstring& warningMessage);
		[[nodiscard]] bool ApplyManagedRuntimeApplyPlan(
			const ManagedRuntimeApplyPlan& plan,
			std::wstring& warningMessage);
		void ResetGatewayOwnedRuntimeCleanup();
		void RegisterGatewayOwnedRuntimeCleanup(
			const std::string& name,
			std::function<void()> action);
		void ExecuteGatewayOwnedRuntimeCleanup();
		void ExecuteGatewayStartupFailureCleanup(
			const blazeclaw::config::AppConfig& config,
			const GatewayRuntimeBootstrapCoordinator::StartupResult& startupResult);
		void ExecuteNonGatewayRuntimeCleanup();
		void BeginShutdownPreludeRecording();
		void RecordShutdownPreludePhase(const std::string& phaseName);
		void FinalizeShutdownPreludeEvidence();
		void RecordGatewayLifecycleTransition(const std::string& transition);
		void RecordGatewayStartupConfigSnapshot();
		void RefreshGatewayAuthBootstrapDiagnostics(
			const blazeclaw::config::AppConfig& config);
		void QueueManagedConfigInternalWriteHash(std::uint64_t hash);
		[[nodiscard]] std::optional<std::uint64_t>
			ConsumeManagedConfigInternalWriteHash();

		[[nodiscard]] ChatProviderRuntimeBindings BuildChatProviderRuntimeBindings();
		void RefreshOpenClawOriginalRuntimeTools(const blazeclaw::config::AppConfig& config);
		void EmitOpenClawOriginalTelemetry() const;

	public:
		// Provider credential/cancellation integration
		void SetActiveChatProvider(
			const std::string& provider,
			const std::string& model);
		[[nodiscard]] bool ApplySpeechRecognitionConfigReload(
			bool speechEnabled,
			const std::wstring& speechProvider,
			const std::wstring& speechStorageRoot,
			const std::wstring& speechActiveModelId,
			const std::wstring& speechModelPath,
			std::string* outStatusMessage = nullptr);
		[[nodiscard]] const std::string& ActiveChatProvider() const noexcept;
		[[nodiscard]] const std::string& ActiveChatModel() const noexcept;
		[[nodiscard]] std::optional<std::string> ResolveDeepSeekCredentialUtf8() const;
		[[nodiscard]] bool HasDeepSeekCredential() const;
		[[nodiscard]] bool IsDeepSeekRunCancelled(const std::string& runId) const;
		void MarkDeepSeekRunCancelled(const std::string& runId);
		void ClearDeepSeekRunCancelled(const std::string& runId);
		[[nodiscard]] bool IsEmbeddedRunCancelled(const std::string& runId) const;
		void MarkEmbeddedRunCancelled(const std::string& runId);
		void ClearEmbeddedRunCancelled(const std::string& runId);


		[[nodiscard]] std::string BuildOperatorDiagnosticsReport() const;
		[[nodiscard]] std::string BuildGatewayParityLifecycleTraceJson() const;

	private:
		// Embedded tooling and provider execution
		[[nodiscard]] std::vector<EmbeddedToolBinding>
			BuildEmbeddedToolBindings() const;
		[[nodiscard]] blazeclaw::gateway::GatewayHost::ChatRuntimeResult
			ExecuteProviderChatRuntimePath(
				const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
				const std::string& sessionId,
				const std::string& runtimeMessage,
				const std::string& activeProvider,
				const std::string& activeModel);

	private:
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
				std::uint64_t queueWaitTimeoutMs = 0;
				std::uint64_t executionTimeoutMs = 0;
			};

			struct GatewayLifecycleState {
				std::string startupMode = "local_runtime_dispatch";
				std::string startupModeSource = "config";
				std::string failedStage;
				bool startupDegraded = false;
				bool managedConfigReloaderStarted = false;
				bool closePreludeExecuted = false;
				bool startupFailureCleanupExecuted = false;
				std::string cleanupPath = "none";
				std::uint64_t authSessionGenerationCurrent = 0;
				std::uint64_t authSessionGenerationRequired = 0;
				std::uint64_t authSessionGenerationRejectCount = 0;
				std::vector<std::string> transitions;
				/// S1: stable ids, aligned with `GatewayMigrationIds`.
				std::vector<std::string> startupMigrationsApplied;
				std::string authBootstrapPathTag;
				std::string authBootstrapStatus;
				std::string authBootstrapDetail;
				/// S2: `ResolveGatewayRuntimeConfig` output (config + env effective policy).
				blazeclaw::config::GatewayResolvedRuntimeConfig resolvedRuntimeConfig;
				/// S5: last shutdown / startup-failure cleanup ordering (see `GatewayShutdownPreludeContract`).
				std::uint64_t gatewayShutdownInvocationCount = 0;
				GatewayShutdownPreludeContract lastShutdownPrelude;
				bool shutdownPreludeRecordingActive = false;
			};

			struct GatewayLiveRuntimeState {
				struct OwnedCleanupEntry {
					std::string name;
					std::function<void()> action;
				};

				bool runtimeStateCreated = false;
				bool runtimeServicesStarted = false;
				bool transportHandlersAttached = false;
				bool runtimeSubscriptionsStarted = false;
				std::wstring managedConfigPath = L"blazeclaw.conf";
				bool managedConfigReloaderEnabled = false;
				bool managedConfigReloaderRunning = false;
				std::uint64_t managedConfigApplyCount = 0;
				std::uint64_t managedConfigRejectCount = 0;
				/// S4.1: last `embedded.extensionSurfaceApplyEpoch` applied to gateway extension surface.
				std::uint64_t lastAppliedExtensionSurfaceEpoch = 0;
				std::uint64_t extensionSurfaceReloadCount = 0;
				std::string lastExtensionSurfaceMethodDeltaJson;
				blazeclaw::config::GatewayStartupConfigFileSnapshot startupConfigSnapshot;
				std::vector<OwnedCleanupEntry> ownedCleanup;
				std::vector<std::string> ownedCleanupOrder;
				std::vector<std::uint64_t> pendingInternalWriteHashes;
			};

			EmbeddedRuntimeState embeddedRuntime;
			EmailPolicyState emailPolicy;
			HooksState hooks;
			ChatRuntimeState chatRuntime;
			GatewayLifecycleState gatewayLifecycle;
			GatewayLiveRuntimeState gatewayLiveRuntime;
		};

		static constexpr std::size_t kChatRuntimeQueueCapacity =
			runtime::contracts::kDefaultQueueCapacity;
		static constexpr std::uint64_t kChatRuntimeQueueWaitTimeoutMs =
			runtime::contracts::kDefaultQueueWaitTimeoutMs;
		static constexpr std::uint64_t kChatRuntimeExecutionTimeoutMs =
			runtime::contracts::kDefaultExecutionTimeoutMs;

		bool m_running = false;
		std::string m_activeChatProvider = "local";
		std::string m_activeChatModel = "default";
		blazeclaw::config::AppConfig m_activeConfig;
		EmailFallbackRuntimeCoordinator m_emailFallbackRuntimeCoordinator;
		EmailPreflightHealthService m_emailPreflightHealthService;
		EmailPolicyOrchestrationService m_emailPolicyOrchestrationService;
		EmailRuntimeDiagnosticsProjector m_emailRuntimeDiagnosticsProjector;
		EmailPolicyOrchestrationService::ResolvedEmailFallbackPolicy
			m_emailFallbackResolvedPolicy;
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
		speechrecognition::SpeechRecognitionRuntime m_speechRecognitionRuntime;
		SpeechTranscriptionCoordinator m_speechTranscriptionCoordinator;
		speechrecognition::SpeechRecognitionRuntimeSnapshot m_speechRecognition;
		texttospeech::TextToSpeechRuntimeSnapshot m_textToSpeech;
		std::unique_ptr<localmodel::ITextGenerationRuntime> m_localModelRuntime;
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
		SkillsFacade m_skillsFacade;
		InlineActionsOrchestrationService m_inlineActionsOrchestrationService;
		ChatRuntimeOrchestrationCoordinator m_chatRuntimeOrchestrationCoordinator;
		SkillsStartupCoordinator m_skillsStartupCoordinator;
		HooksStartupCoordinator m_hooksStartupCoordinator;
		FixtureStartupValidatorFacade m_fixtureStartupValidatorFacade;
		SkillsRunSnapshot m_skillsRunSnapshot;
		SkillCommandInvocationService m_skillCommandInvocationService;
		SkillsCommandService m_skillsCommandService;
		SkillCommandsAggregationService m_skillCommandsAggregationService;
		SkillsCommandSnapshot m_skillsCommands;
		SkillsSyncService m_skillsSyncService;
		SkillsSyncSnapshot m_skillsSync;
		SkillsEnvOverrideService m_skillsEnvOverrideService;
		SkillsEnvOverrideSnapshot m_skillsEnvOverrides;
		SkillsInstallService m_skillsInstallService;
		SkillsInstallSnapshot m_skillsInstall;
		SkillSecurityScanService m_skillSecurityScanService;

		std::unique_ptr<ExtensionBundleCommandSourceAdapter>
			m_extensionBundleCommandSourceAdapter;
		std::size_t m_bundleCommandRootsScannedCount = 0;
		std::size_t m_bundleCommandFilesLoadedCount = 0;
		std::size_t m_bundleCommandFilesSkippedDisabledCount = 0;
		std::size_t m_bundleCommandFilesSkippedEmptyPromptCount = 0;
		std::size_t m_bundleCommandFilesSkippedInvalidNameCount = 0;
		std::size_t m_bundleCommandFilesRejectedUnsafeCount = 0;
		std::vector<std::string> m_effectiveSkillRoots;
		SkillSecurityScanSnapshot m_skillSecurityScan;
		ConfigSchemaService m_configSchemaService;
		SkillsWatchService m_skillsWatchService;
		SkillsWatchSnapshot m_skillsWatch;
		SkillsGatewayMethodHandler m_skillsGatewayMethodHandler;
		SkillsGatewayProjectionService m_skillsGatewayProjectionService;
		ManagedRuntimeConfigDiffCoordinator m_managedRuntimeConfigDiffCoordinator;
		GatewayLifecycleDiagnosticsProjector m_gatewayLifecycleDiagnosticsProjector;
		EmbeddedRuntimeDiagnosticsProjector m_embeddedRuntimeDiagnosticsProjector;
		HooksDiagnosticsProjector m_hooksDiagnosticsProjector;
		ModelRuntimeDiagnosticsProjector m_modelRuntimeDiagnosticsProjector;
		SkillsHostCallbacks m_skillsHostCallbacks;
		CChatRuntime m_chatRuntime;
		CDeepSeekClient m_deepSeekClient;
		CSkillsHooksCoordinator m_skillsHooksCoordinator;
		CServiceBootstrapCoordinator m_serviceBootstrapCoordinator;
		GatewayRuntimeBootstrapCoordinator m_gatewayRuntimeBootstrapCoordinator;
		GatewayManagedConfigReloader m_gatewayManagedConfigReloader;
		CDiagnosticsReportBuilder m_diagnosticsReportBuilder;
		ChatProviderRuntimeService m_chatProviderRuntimeService;
		OperatorDiagnosticsAssembler m_operatorDiagnosticsAssembler;
		CToolRuntimeRegistry m_toolRuntimeRegistry;
		blazeclaw::gateway::GatewayHost m_gatewayHost;
		blazeclaw::gateway::SkillsCatalogGatewayState m_gatewaySkillsStateProjection;

		mutable std::mutex m_deepSeekCancelMutex;
		mutable std::unordered_map<std::string, bool> m_deepSeekCancelledRuns;
		mutable std::mutex m_embeddedCancelMutex;
		mutable std::unordered_map<std::string, bool> m_embeddedCancelledRuns;
	};

} // namespace blazeclaw::core
