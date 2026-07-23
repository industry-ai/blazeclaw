#include "pch.h"
#include "ServiceLifecycleStartupCoordinator.h"

#include "ServiceManager.h"
#include "bootstrap/CServiceBootstrapCoordinator.h"
#include "runtime/CChatRuntime.h"
#include "runtime/LocalModel/LlamaTextGenerationRuntime.h"
#include "runtime/LocalModel/OnnxTextGenerationRuntime.h"
#include "runtime/SpeechRecognition/SpeechRecognitionRuntime.h"
#include "startup/ServiceStartupPhaseModule.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string>

#include <Windows.h>
#include <thread>

namespace blazeclaw::core {

namespace {

std::wstring ToWide(const std::string& value) {
	if (value.empty()) {
		return {};
	}

	const int needed = MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if (needed <= 0) {
		return {};
	}

	std::wstring output(static_cast<std::size_t>(needed), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		output.data(),
		needed);
	return output;
}

void AppendStartupTrace(const char* stage) {
	CServiceBootstrapCoordinator coordinator;
	coordinator.AppendStartupTrace(stage);
}

std::string ToNarrow(const std::wstring& value) {
	std::string output;
	output.reserve(value.size());
	for (const wchar_t ch : value) {
		output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
	}
	return output;
}

bool IsLlamaLocalModelId(const std::string& modelId) {
	return modelId.rfind("llama/", 0) == 0;
}

std::wstring ToLower(const std::wstring& value) {
	std::wstring lowered = value;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](const wchar_t ch) {
			return static_cast<wchar_t>(std::towlower(ch));
		});
	return lowered;
}

std::filesystem::path ResolveWorkspaceRootForSkills(
	const std::filesystem::path& startPath) {
	std::error_code ec;
	auto cursor = std::filesystem::absolute(startPath, ec);
	if (ec) {
		return startPath;
	}

	while (!cursor.empty()) {
		const auto directSkills = cursor / L"skills";
		if (std::filesystem::is_directory(directSkills, ec) && !ec) {
			return cursor;
		}

		const auto nestedSkills = cursor / L"blazeclaw" / L"skills";
		if (std::filesystem::is_directory(nestedSkills, ec) && !ec) {
			return cursor;
		}

		if (!cursor.has_parent_path()) {
			break;
		}

		auto parent = cursor.parent_path();
		if (parent == cursor) {
			break;
		}

		cursor = parent;
	}

	return startPath;
}

} // namespace

void ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies(
	ServiceManager& manager,
	const blazeclaw::config::AppConfig& config) {
	startup::ServiceStartupPhaseModuleRunner::Execute(
		startup::ServiceStartupPhaseModule{
			.id = "configure_policies",
			.execute = [&manager, &config]() {
		manager.m_activeConfig = config;
		manager.m_activeChatProvider = config.chat.activeProvider.empty()
			? "local"
			: ToNarrow(config.chat.activeProvider);
		manager.m_activeChatModel = config.chat.activeModel.empty()
			? "default"
			: ToNarrow(config.chat.activeModel);

		if (manager.m_activeChatProvider == "local" &&
			IsLlamaLocalModelId(manager.m_activeChatModel)) {
			manager.m_activeConfig.localModel.provider = L"llama.cpp";
		}
		const auto hooksPolicy =
			manager.m_serviceBootstrapCoordinator.ResolveHooksPolicySettings(manager.m_activeConfig);
		manager.m_state.hooks.engineEnabled = hooksPolicy.engineEnabled;
		manager.m_state.hooks.fallbackPromptInjection = hooksPolicy.fallbackPromptInjection;
		manager.m_state.hooks.reminderEnabled = hooksPolicy.reminderEnabled;
		manager.m_state.hooks.reminderVerbosity = hooksPolicy.reminderVerbosity;
		manager.m_state.hooks.allowedPackages = hooksPolicy.allowedPackages;
		manager.m_state.hooks.strictPolicyEnforcement = hooksPolicy.strictPolicyEnforcement;
		manager.m_state.hooks.governanceReportingEnabled =
			hooksPolicy.governanceReportingEnabled;
		manager.m_state.hooks.governanceReportDir = hooksPolicy.governanceReportDir;
		manager.m_state.hooks.autoRemediationEnabled = hooksPolicy.autoRemediationEnabled;
		manager.m_state.hooks.autoRemediationRequiresApproval =
			hooksPolicy.autoRemediationRequiresApproval;
		manager.m_state.hooks.autoRemediationApprovalToken =
			hooksPolicy.autoRemediationApprovalToken;
		manager.m_state.hooks.autoRemediationTenantId = hooksPolicy.autoRemediationTenantId;
		manager.m_state.hooks.autoRemediationPlaybookDir =
			hooksPolicy.autoRemediationPlaybookDir;
		manager.m_state.hooks.autoRemediationTokenMaxAgeMinutes =
			hooksPolicy.autoRemediationTokenMaxAgeMinutes;
		manager.m_state.hooks.remediationTelemetryEnabled =
			hooksPolicy.remediationTelemetryEnabled;
		manager.m_state.hooks.remediationTelemetryDir = hooksPolicy.remediationTelemetryDir;
		manager.m_state.hooks.remediationAuditEnabled = hooksPolicy.remediationAuditEnabled;
		manager.m_state.hooks.remediationAuditDir = hooksPolicy.remediationAuditDir;
		manager.m_state.hooks.remediationSloMaxDriftDetected =
			hooksPolicy.remediationSloMaxDriftDetected;
		manager.m_state.hooks.remediationSloMaxPolicyBlocked =
			hooksPolicy.remediationSloMaxPolicyBlocked;
		manager.m_state.hooks.complianceAttestationEnabled =
			hooksPolicy.complianceAttestationEnabled;
		manager.m_state.hooks.complianceAttestationDir = hooksPolicy.complianceAttestationDir;
		manager.m_state.hooks.enterpriseSlaGovernanceEnabled =
			hooksPolicy.enterpriseSlaGovernanceEnabled;
		manager.m_state.hooks.enterpriseSlaPolicyId = hooksPolicy.enterpriseSlaPolicyId;
		manager.m_state.hooks.crossTenantAttestationAggregationEnabled =
			hooksPolicy.crossTenantAttestationAggregationEnabled;
		manager.m_state.hooks.crossTenantAttestationAggregationDir =
			hooksPolicy.crossTenantAttestationAggregationDir;
		manager.m_state.hooks.governanceReportsGenerated = 0;
		manager.m_state.hooks.lastGovernanceReportPath.clear();
		manager.m_state.hooks.autoRemediationExecuted = 0;
		manager.m_state.hooks.lastAutoRemediationStatus = L"idle";
		manager.m_state.hooks.lastAutoRemediationPlaybookPath.clear();
		manager.m_state.hooks.autoRemediationTokenRotations = 0;
		manager.m_state.hooks.lastRemediationTelemetryPath.clear();
		manager.m_state.hooks.lastRemediationAuditPath.clear();
		manager.m_state.hooks.remediationSloStatus = L"unknown";
		manager.m_state.hooks.lastComplianceAttestationPath.clear();
		manager.m_state.hooks.lastCrossTenantAttestationAggregationPath.clear();
		manager.m_state.hooks.crossTenantAttestationAggregationCount = 0;
		manager.m_state.hooks.crossTenantAttestationAggregationStatus = L"idle";

		const auto emailPolicy =
			manager.m_serviceBootstrapCoordinator.ResolveEmailPolicySettings(manager.m_activeConfig);
		manager.m_emailFallbackResolvedPolicy =
			manager.m_emailPolicyOrchestrationService.ResolveFallbackPolicy(
				manager.m_activeConfig,
				L"email.schedule",
				L"email.send");
		manager.m_state.emailPolicy.rolloutMode = emailPolicy.rolloutMode;
		manager.m_state.emailPolicy.enforceChannel = emailPolicy.enforceChannel;
		manager.m_state.emailPolicy.rollbackBridgeEnabled = emailPolicy.rollbackBridgeEnabled;
		manager.m_state.emailPolicy.canaryEligible = emailPolicy.canaryEligible;
		manager.m_state.emailPolicy.runtimeEnabled = emailPolicy.runtimeEnabled;
		manager.m_state.emailPolicy.runtimeEnforce = emailPolicy.runtimeEnforce;

		if (manager.m_state.emailPolicy.runtimeEnabled &&
			!manager.m_activeConfig.email.policyProfiles.enabled) {
			manager.m_skillsCatalog.diagnostics.warnings.push_back(
				L"email policy rollout gate activated runtime policy profile monitor/enforce mode.");
		}
		AppendStartupTrace("ServiceManager.Start.policy.ready");
			},
		});
}

void ServiceLifecycleStartupCoordinator::RunInitializeModules(ServiceManager& manager) {
	startup::ServiceStartupPhaseModuleRunner::Execute(
		startup::ServiceStartupPhaseModule{
			.id = "initialize_modules",
			.execute = [&manager]() {
		manager.m_agentsScope = manager.m_agentsCatalogService.BuildSnapshot(
			std::filesystem::current_path(),
			manager.m_activeConfig);
		manager.m_agentsWorkspace = manager.m_agentsWorkspaceService.BuildSnapshot(manager.m_agentsScope);
		manager.m_agentsToolPolicy = manager.m_agentsToolPolicyService.BuildSnapshot(
			manager.m_agentsScope,
			manager.m_activeConfig);
		manager.m_agentsShellRuntimeService.Configure(manager.m_activeConfig);
		manager.m_agentsModelRoutingService.Configure(manager.m_activeConfig);
		manager.m_modelRouting = manager.m_agentsModelRoutingService.Snapshot();
		manager.m_agentsAuthProfileService.Configure(manager.m_activeConfig);
		manager.m_agentsAuthProfileService.Initialize(std::filesystem::current_path());
		manager.m_authProfiles = manager.m_agentsAuthProfileService.Snapshot(1735690000000);
		manager.m_sandbox = manager.m_agentsSandboxService.BuildSnapshot(
			manager.m_agentsScope,
			manager.m_activeConfig);
		manager.m_agentsTranscriptSafetyService.Configure(manager.m_activeConfig);
		manager.m_subagentRegistryService.Configure(manager.m_activeConfig);
		manager.m_subagentRegistryService.Initialize(std::filesystem::current_path());
		manager.m_subagentRegistry = manager.m_subagentRegistryService.Snapshot();
		manager.m_lastAcpDecision = manager.m_acpSpawnService.Evaluate(
			manager.m_activeConfig,
			AcpSpawnRequest{
				.requesterSessionId = "main",
				.requesterAgentId = "default",
				.targetAgentId = "",
				.threadRequested = false,
				.requesterSandboxed = false,
			});
		AppendStartupTrace("ServiceManager.Start.agents.ready");
		manager.m_embeddingsService.Configure(manager.m_activeConfig);
		manager.m_embeddings = manager.m_embeddingsService.Snapshot();
		AppendStartupTrace("ServiceManager.Start.embeddings.ready");

		manager.m_speechRecognitionRuntime.Configure(manager.m_activeConfig);
		manager.m_speechRecognition = manager.m_speechRecognitionRuntime.Snapshot();
		AppendStartupTrace("ServiceManager.Start.speech.configured");

		manager.m_localModelRolloutEligible = manager.IsLocalModelRolloutEligible();
		manager.m_localModelActivationEnabled = false;
		manager.m_localModelActivationReason.clear();

		if (!manager.m_activeConfig.localModel.enabled) {
			manager.m_localModelActivationReason = "config_disabled";
		}
		else if (!manager.m_localModelRolloutEligible) {
			manager.m_localModelActivationReason = "rollout_stage_not_eligible";
		}

		const std::wstring provider =
			ToLower(manager.m_activeConfig.localModel.provider);
		const bool activeSelectionWantsLlama =
			manager.m_activeChatProvider == "local" &&
			IsLlamaLocalModelId(manager.m_activeChatModel);
		const bool useLlamaRuntime =
			activeSelectionWantsLlama ||
			provider == L"llama" ||
			provider == L"llama.cpp";
		if (useLlamaRuntime) {
			manager.m_activeConfig.localModel.provider = L"llama.cpp";
		}
		if (useLlamaRuntime) {
			manager.m_localModelRuntime =
				std::make_unique<localmodel::LlamaTextGenerationRuntime>();
		}
		else {
			manager.m_localModelRuntime =
				std::make_unique<localmodel::OnnxTextGenerationRuntime>();
		}

		manager.m_localModelRuntime->Configure(manager.m_activeConfig);
		const auto runtimeOrchestrationPolicy =
			manager.m_serviceBootstrapCoordinator.ResolveRuntimeOrchestrationPolicySettings();
		const bool localModelStartupLoadEnabled =
			runtimeOrchestrationPolicy.localModelStartupLoadEnabled;
		AppendStartupTrace("ServiceManager.Start.localmodel.beforeLoad");
		bool localModelLoaded = false;
		if (manager.m_activeConfig.localModel.enabled &&
			manager.m_localModelRolloutEligible &&
			localModelStartupLoadEnabled) {
			// Start local model load asynchronously to avoid blocking UI startup.
			manager.m_localModelActivationReason = "startup_load_async_started";
			AppendStartupTrace("ServiceManager.Start.localmodel.asyncLoadStarted");
			std::thread([&manager]() {
				const bool ok = manager.m_localModelRuntime->LoadModel();
				// Refresh snapshot and set activation flags
				manager.m_localModelRuntimeSnapshot = manager.m_localModelRuntime->Snapshot();
				if (ok) {
					manager.m_localModelActivationEnabled = true;
					manager.m_localModelActivationReason.clear();
					AppendStartupTrace("ServiceManager.Start.localmodel.asyncLoadCompleted");
				}
				else {
					manager.m_localModelActivationReason = "async_load_failed";
					AppendStartupTrace("ServiceManager.Start.localmodel.asyncLoadFailed");
				}
			}).detach();
			// Treat as not synchronously loaded for this startup path; background will update state
			localModelLoaded = false;
		}
		else if (manager.m_activeConfig.localModel.enabled &&
			manager.m_localModelRolloutEligible) {
			manager.m_localModelActivationReason = "startup_load_deferred";
		}
		manager.m_localModelRuntimeSnapshot = manager.m_localModelRuntime->Snapshot();
		AppendStartupTrace("ServiceManager.Start.localmodel.afterLoad");
		std::string speechRuntimeHotMode = "always_online";
		{
			std::wstring mode = manager.m_activeConfig.speechRecognition.runtimeHotMode;
			std::transform(
				mode.begin(),
				mode.end(),
				mode.begin(),
				[](wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			if (mode == L"on_demand" || mode == L"idle_timeout") {
				speechRuntimeHotMode.clear();
				speechRuntimeHotMode.reserve(mode.size());
				for (const wchar_t ch : mode) {
					speechRuntimeHotMode.push_back(
						ch <= 0x7F
						? static_cast<char>(ch)
						: '?');
				}
			}
		}
		const bool speechStartupLoadEnabled =
			speechRuntimeHotMode != "on_demand";
		bool speechRecognitionLoaded = true;
		if (speechStartupLoadEnabled) {
			// Perform speech model load asynchronously to reduce startup blocking.
			manager.m_speechRecognition.status = "loading_async";
			AppendStartupTrace("ServiceManager.Start.speech.asyncLoadStarted");
			std::thread([&manager]() {
				const bool loaded = manager.m_speechRecognitionRuntime.LoadModel();
				manager.m_speechRecognition = manager.m_speechRecognitionRuntime.Snapshot();
				if (loaded) {
					AppendStartupTrace("ServiceManager.Start.speech.asyncLoadCompleted");
				}
				else {
					AppendStartupTrace("ServiceManager.Start.speech.asyncLoadFailed");
				}
			}).detach();
			// Mark as started for startup flow; actual readiness will be reflected in the snapshot.
			speechRecognitionLoaded = true;
		}
		else {
			speechRecognitionLoaded = true;
		}
		manager.m_speechRecognition = manager.m_speechRecognitionRuntime.Snapshot();
		if (!speechRecognitionLoaded && manager.m_speechRecognition.status.empty()) {
			manager.m_speechRecognition.status = "load_failed";
		}
		if (speechStartupLoadEnabled == false) {
			manager.m_speechRecognition.status = "startup_load_deferred";
		}
		AppendStartupTrace("ServiceManager.Start.speech.afterLoad");
		if (!localModelLoaded && manager.m_localModelRuntimeSnapshot.status.empty()) {
			manager.m_localModelRuntimeSnapshot.status = localModelStartupLoadEnabled
				? "load_failed"
				: "startup_load_deferred";
		}

		if (manager.m_activeConfig.localModel.enabled &&
			manager.m_localModelRolloutEligible &&
			localModelLoaded &&
			manager.m_localModelRuntimeSnapshot.ready) {
			manager.m_localModelActivationEnabled = true;
			manager.m_localModelActivationReason = "active";
		}
		else if (manager.m_activeConfig.localModel.enabled &&
			manager.m_localModelRolloutEligible &&
			!manager.m_localModelRuntimeSnapshot.ready) {
			manager.m_localModelActivationReason = localModelStartupLoadEnabled
				? "initialization_failed"
				: "startup_load_deferred";
		}

		TRACE(
			"[LocalModel] startup.gating enabled=%s rolloutEligible=%s activation=%s reason=%s stage=%S status=%s\n",
			manager.m_activeConfig.localModel.enabled ? "true" : "false",
			manager.m_localModelRolloutEligible ? "true" : "false",
			manager.m_localModelActivationEnabled ? "true" : "false",
			manager.m_localModelActivationReason.c_str(),
			manager.m_activeConfig.localModel.rolloutStage.c_str(),
			manager.m_localModelRuntimeSnapshot.status.c_str());

		if (manager.m_localModelActivationEnabled && manager.m_localModelRuntimeSnapshot.ready) {
			std::string localContractFailure;
			if (!manager.m_localModelRuntime->VerifyDeterministicContract(localContractFailure)) {
				manager.m_localModelRuntimeSnapshot = manager.m_localModelRuntime->Snapshot();
				const bool enforceContract =
					_wcsicmp(manager.m_activeConfig.localModel.rolloutStage.c_str(), L"dev") != 0;
				manager.m_localModelRuntimeSnapshot.status = enforceContract
					? "contract_verification_failed"
					: "contract_verification_warning";
				manager.m_localModelActivationEnabled = !enforceContract;
				manager.m_localModelActivationReason = enforceContract
					? "contract_verification_failed"
					: "active_contract_warning";
				if (!localContractFailure.empty()) {
					manager.m_localModelRuntimeSnapshot.error = localmodel::TextGenerationError{
						.code = localmodel::TextGenerationErrorCode::InferenceFailed,
						.message = localContractFailure,
					};
				}

				manager.m_skillsCatalog.diagnostics.warnings.push_back(
					L"local-model deterministic contract verification failed");
			}
		}

		manager.m_retrievalMemoryService.Configure(manager.m_activeConfig);
		manager.m_retrievalMemory = manager.m_retrievalMemoryService.Snapshot();
		manager.m_piEmbeddedService.Configure(manager.m_activeConfig);
		AppendStartupTrace("ServiceManager.Start.retrieval.ready");
		manager.m_state.embeddedRuntime.dynamicLoopCanaryProviders =
			runtimeOrchestrationPolicy.dynamicLoopCanaryProviders;
		manager.m_state.embeddedRuntime.dynamicLoopCanarySessions =
			runtimeOrchestrationPolicy.dynamicLoopCanarySessions;
		manager.m_state.embeddedRuntime.dynamicLoopPromotionMinRuns =
			runtimeOrchestrationPolicy.dynamicLoopPromotionMinRuns;
		manager.m_state.embeddedRuntime.dynamicLoopPromotionMinSuccessRate =
			runtimeOrchestrationPolicy.dynamicLoopPromotionMinSuccessRate;
		manager.m_state.embeddedRuntime.runSuccessCount = 0;
		manager.m_state.embeddedRuntime.runFailureCount = 0;
		manager.m_state.embeddedRuntime.runTimeoutCount = 0;
		manager.m_state.embeddedRuntime.runCancelledCount = 0;
		manager.m_state.embeddedRuntime.runFallbackCount = 0;
		manager.m_state.embeddedRuntime.taskDeltaTransitionCount = 0;
		manager.m_state.embeddedRuntime.emailFallbackAttemptCount = 0;
		manager.m_state.embeddedRuntime.emailFallbackSuccessCount = 0;
		manager.m_state.embeddedRuntime.emailFallbackFailureCount = 0;
		manager.m_state.embeddedRuntime.lastDynamicLoopEnabled = false;
		manager.m_state.embeddedRuntime.lastCanaryEligible = false;
		manager.m_state.embeddedRuntime.lastPromotionReady = false;
		manager.m_state.embeddedRuntime.lastFallbackUsed = false;
		manager.m_state.embeddedRuntime.lastFallbackReason.clear();
		const auto runtimeQueueSettings =
			manager.m_serviceBootstrapCoordinator.ResolveRuntimeQueueSettings(
				ServiceManager::kChatRuntimeQueueWaitTimeoutMs,
				ServiceManager::kChatRuntimeExecutionTimeoutMs);
		manager.m_state.chatRuntime.asyncQueueEnabled = runtimeQueueSettings.asyncQueueEnabled;
		manager.m_state.chatRuntime.queueWaitTimeoutMs = runtimeQueueSettings.queueWaitTimeoutMs;
		manager.m_state.chatRuntime.executionTimeoutMs = runtimeQueueSettings.executionTimeoutMs;
		manager.m_chatRuntime.Initialize(
			CChatRuntime::Dependencies{
				.isRunCancelled = [&manager](
					const std::string& runId,
					const std::string& provider)
					{
						return manager.IsEmbeddedRunCancelled(runId) ||
							(provider == "deepseek" && manager.IsDeepSeekRunCancelled(runId));
					},
				.onQueueTimeout = [&manager](
					const std::string& runId,
					const std::string& provider)
					{
						manager.MarkEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							manager.MarkDeepSeekRunCancelled(runId);
						}
					},
				.onQueueTimeoutCleanup = [&manager](
					const std::string& runId,
					const std::string& provider)
					{
						manager.ClearEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							manager.ClearDeepSeekRunCancelled(runId);
						}
					},
				.onAbort = [&manager](
					const std::string& runId,
					const std::string& provider,
					const bool removedQueuedJob)
					{
						manager.MarkEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							manager.MarkDeepSeekRunCancelled(runId);
						}
						if (removedQueuedJob)
						{
							manager.ClearEmbeddedRunCancelled(runId);
							manager.ClearDeepSeekRunCancelled(runId);
						}
					},
				.onWorkerCompleted = [&manager](
					const std::string& runId,
					const std::string& provider)
					{
						manager.ClearEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							manager.ClearDeepSeekRunCancelled(runId);
						}
					},
				.cancelActiveRuntime = [&manager](const std::string& runId)
					{
						if (manager.m_localModelActivationEnabled)
						{
						   const bool cancelled =
								manager.m_localModelRuntime != nullptr &&
								manager.m_localModelRuntime->Cancel(runId);
							if (manager.m_localModelRuntime != nullptr) {
								manager.m_localModelRuntimeSnapshot =
									manager.m_localModelRuntime->Snapshot();
							}
							return cancelled;
						}
						return false;
					},
			},
			CChatRuntime::Config{
				.queueCapacity = ServiceManager::kChatRuntimeQueueCapacity,
				.queueWaitTimeoutMs = manager.m_state.chatRuntime.queueWaitTimeoutMs,
				.executionTimeoutMs = manager.m_state.chatRuntime.executionTimeoutMs,
				.asyncQueueEnabled = manager.m_state.chatRuntime.asyncQueueEnabled,
				.errorQueueFull = runtime::contracts::kErrorQueueFull,
				.errorCancelled = runtime::contracts::kErrorCancelled,
				.errorTimedOut = runtime::contracts::kErrorTimedOut,
				.errorWorkerUnavailable = runtime::contracts::kErrorWorkerUnavailable,
			});
		if (manager.m_state.chatRuntime.asyncQueueEnabled) {
			const bool chatRuntimeWorkerStarted = manager.m_chatRuntime.StartWorker();
			if (!chatRuntimeWorkerStarted) {
				TRACE(
					"[ChatRuntime] worker unavailable; callbacks will return %s\n",
					runtime::contracts::kErrorWorkerUnavailable);
			}
		}
		else {
			TRACE(
				"[ChatRuntime] async queue disabled by rollout flag; using synchronous runtime path\n");
		}
		const bool startupSkillsRefreshEnabled =
			runtimeOrchestrationPolicy.startupSkillsRefreshEnabled;
		manager.m_skillsStartupCoordinator.Execute(
			SkillsStartupCoordinator::ExecutionContext{
				.startupSkillsRefreshEnabled = startupSkillsRefreshEnabled,
				.runFullRefresh = [&manager]() {
					manager.RefreshSkillsState(manager.m_activeConfig, true, L"startup");
					AppendStartupTrace("ServiceManager.Start.skills.refreshed");
				},
				.runMinimalRefresh = [&manager]() {
					manager.RefreshSkillsState(manager.m_activeConfig, true, L"startup-minimal");
					manager.m_skillsCatalog.diagnostics.warnings.push_back(
						L"skills startup full refresh skipped; minimal startup catalog loaded.");
					AppendStartupTrace("ServiceManager.Start.skills.refresh.minimal");
				},
				.onStdException = [&manager](const std::exception& ex) {
					manager.m_skillsCatalog = SkillsCatalogSnapshot{};
					manager.m_skillsEligibility = SkillsEligibilitySnapshot{};
					manager.m_skillsPrompt = SkillsPromptSnapshot{};
					manager.m_skillsRunSnapshot = SkillsRunSnapshot{};
					manager.m_skillsCommands = SkillsCommandSnapshot{};
					manager.m_skillsSync = SkillsSyncSnapshot{};
					manager.m_skillsEnvOverrides = SkillsEnvOverrideSnapshot{};
					manager.m_skillsInstall = SkillsInstallSnapshot{};
					manager.m_skillSecurityScan = SkillSecurityScanSnapshot{};
					manager.m_skillsWatch = SkillsWatchSnapshot{};
					manager.m_hookCatalog = manager.m_hookCatalogService.BuildSnapshot(manager.m_skillsCatalog);
					manager.m_hookExecution = manager.m_hookExecutionService.Snapshot();
					manager.m_hookEvents = manager.m_hookEventService.Snapshot();
					manager.m_skillsCatalog.diagnostics.warnings.push_back(
						L"skills refresh failed during startup; continuing with empty skill snapshots: " +
						ToWide(ex.what()));
					AppendStartupTrace("ServiceManager.Start.skills.refresh.exception");
				},
				.onUnknownException = [&manager]() {
					manager.m_skillsCatalog = SkillsCatalogSnapshot{};
					manager.m_skillsEligibility = SkillsEligibilitySnapshot{};
					manager.m_skillsPrompt = SkillsPromptSnapshot{};
					manager.m_skillsRunSnapshot = SkillsRunSnapshot{};
					manager.m_skillsCommands = SkillsCommandSnapshot{};
					manager.m_skillsSync = SkillsSyncSnapshot{};
					manager.m_skillsEnvOverrides = SkillsEnvOverrideSnapshot{};
					manager.m_skillsInstall = SkillsInstallSnapshot{};
					manager.m_skillSecurityScan = SkillSecurityScanSnapshot{};
					manager.m_skillsWatch = SkillsWatchSnapshot{};
					manager.m_hookCatalog = manager.m_hookCatalogService.BuildSnapshot(manager.m_skillsCatalog);
					manager.m_hookExecution = manager.m_hookExecutionService.Snapshot();
					manager.m_hookEvents = manager.m_hookEventService.Snapshot();
					manager.m_skillsCatalog.diagnostics.warnings.push_back(
						L"skills refresh failed during startup with unknown exception; continuing with empty skill snapshots.");
					AppendStartupTrace("ServiceManager.Start.skills.refresh.exception.unknown");
				},
				.appendTrace = [](const char* stage) {
					AppendStartupTrace(stage);
				},
			});

		const bool startupHookBootstrapEnabled =
			runtimeOrchestrationPolicy.startupHookBootstrapEnabled;
		manager.m_hooksStartupCoordinator.Execute(
			HooksStartupCoordinator::ExecutionContext{
				.startupSkillsRefreshEnabled = startupSkillsRefreshEnabled,
				.startupHookBootstrapEnabled = startupHookBootstrapEnabled,
				.runHookBootstrap = [&manager]() {
					std::wstring hookEventError;
					const bool emittedBootstrapEvent =
						manager.m_hookEventService.EmitAgentBootstrap(
							L"main",
							std::vector<HookBootstrapFile>{
								HookBootstrapFile{.path = L"BOOTSTRAP.md", .virtualFile = true },
								HookBootstrapFile{.path = L"MEMORY.md", .virtualFile = true } },
							hookEventError);
					manager.m_hookEvents = manager.m_hookEventService.Snapshot();
					if (!emittedBootstrapEvent && !hookEventError.empty()) {
						manager.m_skillsCatalog.diagnostics.warnings.push_back(
							L"hooks-event emission failed: " + hookEventError);
					}

					if (manager.m_state.hooks.engineEnabled &&
						emittedBootstrapEvent &&
						!manager.m_hookEvents.events.empty()) {
						std::wstring dispatchError;
						const auto& latestEvent = manager.m_hookEvents.events.back();
						HookLifecycleEvent eventForDispatch = latestEvent;
						if (manager.m_state.hooks.reminderVerbosity == L"detailed") {
							eventForDispatch.bootstrapFiles.push_back(
								HookBootstrapFile{
									.path = L"HOOK_RUNTIME_DETAILED_CONTEXT.md",
									.virtualFile = true,
								});
						}
						if (!manager.m_hookExecutionService.Dispatch(
							eventForDispatch,
							manager.m_hookCatalog,
							HookExecutionPolicy{
								.reminderEnabled = manager.m_state.hooks.reminderEnabled,
								.reminderVerbosity = manager.m_state.hooks.reminderVerbosity,
								.allowedPackages = manager.m_state.hooks.allowedPackages,
								.strictPolicyEnforcement =
									manager.m_state.hooks.strictPolicyEnforcement,
							},
							dispatchError) &&
							!dispatchError.empty()) {
							manager.m_skillsCatalog.diagnostics.warnings.push_back(
								L"hooks-dispatch failed: " + dispatchError);
						}

						manager.m_hookExecution = manager.m_hookExecutionService.Snapshot();
						manager.m_skillsHooksCoordinator.EmitGovernanceAndRemediation(
							HooksGovernanceEmitter::GovernanceContext{
								.execution = manager.m_hookExecution,
								.governanceReportingEnabled =
									manager.m_state.hooks.governanceReportingEnabled,
								.governanceReportDir =
									manager.m_state.hooks.governanceReportDir,
								.allowedPackages = manager.m_state.hooks.allowedPackages,
								.strictPolicyEnforcement =
									manager.m_state.hooks.strictPolicyEnforcement,
								.governanceReportsGenerated =
									manager.m_state.hooks.governanceReportsGenerated,
								.lastGovernanceReportPath =
									manager.m_state.hooks.lastGovernanceReportPath,
							},
							HooksGovernanceEmitter::RemediationContext{
								.execution = manager.m_hookExecution,
								.autoRemediationEnabled =
									manager.m_state.hooks.autoRemediationEnabled,
								.autoRemediationTenantId =
									manager.m_state.hooks.autoRemediationTenantId,
								.autoRemediationPlaybookDir =
									manager.m_state.hooks.autoRemediationPlaybookDir,
								.autoRemediationTokenMaxAgeMinutes =
									manager.m_state.hooks.autoRemediationTokenMaxAgeMinutes,
								.autoRemediationApprovalToken =
									manager.m_state.hooks.autoRemediationApprovalToken,
								.autoRemediationTokenRotations =
									manager.m_state.hooks.autoRemediationTokenRotations,
								.remediationTelemetryEnabled =
									manager.m_state.hooks.remediationTelemetryEnabled,
								.remediationTelemetryDir =
									manager.m_state.hooks.remediationTelemetryDir,
								.remediationAuditEnabled =
									manager.m_state.hooks.remediationAuditEnabled,
								.remediationAuditDir =
									manager.m_state.hooks.remediationAuditDir,
								.remediationSloMaxDriftDetected =
									manager.m_state.hooks.remediationSloMaxDriftDetected,
								.remediationSloMaxPolicyBlocked =
									manager.m_state.hooks.remediationSloMaxPolicyBlocked,
								.complianceAttestationEnabled =
									manager.m_state.hooks.complianceAttestationEnabled,
								.complianceAttestationDir =
									manager.m_state.hooks.complianceAttestationDir,
								.enterpriseSlaPolicyId =
									manager.m_state.hooks.enterpriseSlaPolicyId,
								.crossTenantAttestationAggregationEnabled =
									manager.m_state.hooks.crossTenantAttestationAggregationEnabled,
								.crossTenantAttestationAggregationDir =
									manager.m_state.hooks.crossTenantAttestationAggregationDir,
								.lastGovernanceReportPath =
									manager.m_state.hooks.lastGovernanceReportPath,
								.lastAutoRemediationPlaybookPath =
									manager.m_state.hooks.lastAutoRemediationPlaybookPath,
								.lastAutoRemediationStatus =
									manager.m_state.hooks.lastAutoRemediationStatus,
								.lastRemediationTelemetryPath =
									manager.m_state.hooks.lastRemediationTelemetryPath,
								.lastRemediationAuditPath =
									manager.m_state.hooks.lastRemediationAuditPath,
								.remediationSloStatus = manager.m_state.hooks.remediationSloStatus,
								.lastComplianceAttestationPath =
									manager.m_state.hooks.lastComplianceAttestationPath,
								.crossTenantAttestationAggregationCount =
									manager.m_state.hooks.crossTenantAttestationAggregationCount,
								.crossTenantAttestationAggregationStatus =
									manager.m_state.hooks.crossTenantAttestationAggregationStatus,
								.lastCrossTenantAttestationAggregationPath =
									manager.m_state.hooks
										.lastCrossTenantAttestationAggregationPath,
							},
							manager.m_skillsCatalog.diagnostics.warnings);
						auto hookBootstrapProjection =
							CSkillsHooksCoordinator::HookBootstrapProjectionContext{
								.bootstrapFiles = manager.m_hookExecution.bootstrapFiles,
								.prompt = manager.m_skillsPrompt.prompt,
								.promptChars = manager.m_skillsPrompt.promptChars,
								.promptTruncated = manager.m_skillsPrompt.truncated,
								.maxSkillsPromptChars =
									manager.m_activeConfig.skills.limits.maxSkillsPromptChars,
								.lastReminderState =
									manager.m_hookExecution.diagnostics.lastReminderState,
								.lastReminderReason =
									manager.m_hookExecution.diagnostics.lastReminderReason,
								.selfEvolvingHookTriggered =
									manager.m_state.hooks.selfEvolvingHookTriggered,
							};
						manager.m_skillsHooksCoordinator.ApplyHookBootstrapProjection(
							hookBootstrapProjection);
					}
					else if (!manager.m_state.hooks.engineEnabled) {
						++manager.m_hookExecution.diagnostics.skippedCount;
						manager.m_hookExecution.diagnostics.lastReminderState = L"reminder_skipped";
						manager.m_hookExecution.diagnostics.lastReminderReason =
							L"hook_engine_disabled";
					}
				},
				.appendTrace = [](const char* stage) {
					AppendStartupTrace(stage);
				},
			});

		const std::vector<std::filesystem::path> fixtureCandidates = {
			  std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
			  std::filesystem::current_path() / L"fixtures" / L"agents",
			  std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
			  std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
		};

		const bool startupFixtureValidationEnabled =
			runtimeOrchestrationPolicy.startupFixtureValidationEnabled;
		manager.m_fixtureStartupValidatorFacade.Execute(
			FixtureStartupValidatorFacade::ExecutionContext{
				.startupFixtureValidationEnabled =
					startupFixtureValidationEnabled,
				.runValidation = [&manager, startupFixtureValidationEnabled, fixtureCandidates]() {
					manager.m_serviceBootstrapCoordinator.ValidateStartupFixtures(
						CServiceBootstrapCoordinator::FixtureValidationContext{
							.enabled = startupFixtureValidationEnabled,
							.fixtureCandidates = fixtureCandidates,
							.warnings = manager.m_skillsCatalog.diagnostics.warnings,
							.agentsCatalogService = manager.m_agentsCatalogService,
							.agentsWorkspaceService = manager.m_agentsWorkspaceService,
							.agentsToolPolicyService = manager.m_agentsToolPolicyService,
							.agentsShellRuntimeService = manager.m_agentsShellRuntimeService,
							.agentsModelRoutingService = manager.m_agentsModelRoutingService,
							.agentsAuthProfileService = manager.m_agentsAuthProfileService,
							.agentsSandboxService = manager.m_agentsSandboxService,
							.agentsTranscriptSafetyService =
								manager.m_agentsTranscriptSafetyService,
							.subagentRegistryService = manager.m_subagentRegistryService,
							.acpSpawnService = manager.m_acpSpawnService,
							.embeddingsService = manager.m_embeddingsService,
							.retrievalMemoryService = manager.m_retrievalMemoryService,
							.piEmbeddedService = manager.m_piEmbeddedService,
							.skillsCatalogService = manager.m_skillsCatalogService,
							.skillsEligibilityService = manager.m_skillsEligibilityService,
							.skillsPromptService = manager.m_skillsPromptService,
							.skillsCommandService = manager.m_skillsCommandService,
							.skillsWatchService = manager.m_skillsWatchService,
							.skillsSyncService = manager.m_skillsSyncService,
							.skillsEnvOverrideService = manager.m_skillsEnvOverrideService,
							.skillsFacade = manager.m_skillsFacade,
							.skillsInstallService = manager.m_skillsInstallService,
							.skillSecurityScanService = manager.m_skillSecurityScanService,
							.hookCatalogService = manager.m_hookCatalogService,
							.hookEventService = manager.m_hookEventService,
							.hookExecutionService = manager.m_hookExecutionService,
						});
				},
				.appendTrace = [](const char* stage) {
					AppendStartupTrace(stage);
				},
			});
			},
		});

}

} // namespace blazeclaw::core
