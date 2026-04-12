#include "pch.h"
#include "CDiagnosticsReportBuilder.h"

namespace blazeclaw::core {

	std::string CDiagnosticsReportBuilder::BuildOperatorDiagnosticsReport(
		const DiagnosticsSnapshot& s) const
	{
		const std::string report =
			"{\"runtime\":{\"running\":" +
			std::string(s.runtimeRunning ? "true" : "false") +
			",\"gatewayWarning\":\"" + s.gatewayWarning + "\"" +
			",\"gatewayLifecycle\":{\"startupMode\":\"" +
			s.gatewayStartupMode +
			"\",\"startupModeSource\":\"" +
			s.gatewayStartupModeSource +
			"\",\"startupFailedStage\":\"" +
			s.gatewayStartupFailedStage +
			"\",\"startupDegraded\":" +
			std::string(s.gatewayStartupDegraded ? "true" : "false") +
			",\"managedConfigReloaderStarted\":" +
			std::string(s.gatewayManagedConfigReloaderStarted ? "true" : "false") +
			",\"managedConfigReloaderRunning\":" +
			std::string(s.gatewayManagedConfigReloaderRunning ? "true" : "false") +
			",\"closePreludeExecuted\":" +
			std::string(s.gatewayClosePreludeExecuted ? "true" : "false") +
			",\"runtimeStateCreated\":" +
			std::string(s.gatewayRuntimeStateCreated ? "true" : "false") +
			",\"runtimeServicesStarted\":" +
			std::string(s.gatewayRuntimeServicesStarted ? "true" : "false") +
			",\"transportHandlersAttached\":" +
			std::string(s.gatewayTransportHandlersAttached ? "true" : "false") +
			",\"runtimeSubscriptionsStarted\":" +
			std::string(s.gatewayRuntimeSubscriptionsStarted ? "true" : "false") +
			",\"managedConfigPath\":\"" +
			s.gatewayManagedConfigPath +
			"\",\"managedConfigApplyCount\":" +
			std::to_string(s.gatewayManagedConfigApplyCount) +
			",\"managedConfigRejectCount\":" +
			std::to_string(s.gatewayManagedConfigRejectCount) +
			",\"authSessionGenerationCurrent\":" +
			std::to_string(s.gatewayAuthSessionGenerationCurrent) +
			",\"authSessionGenerationRequired\":" +
			std::to_string(s.gatewayAuthSessionGenerationRequired) +
			",\"authSessionGenerationRejectCount\":" +
			std::to_string(s.gatewayAuthSessionGenerationRejectCount) +
			"}}," +
			"\"emailFallback\":{\"preflightEnabled\":" +
			std::string(s.emailPreflightEnabled ? "true" : "false") +
			",\"policyProfilesEnabled\":" +
			std::string(s.emailPolicyProfilesEnabled ? "true" : "false") +
			",\"policyProfilesEnforce\":" +
			std::string(s.emailPolicyProfilesEnforce ? "true" : "false") +
			",\"policyProfilesRuntimeEnabled\":" +
			std::string(s.emailPolicyProfilesRuntimeEnabled ? "true" : "false") +
			",\"policyProfilesRuntimeEnforce\":" +
			std::string(s.emailPolicyProfilesRuntimeEnforce ? "true" : "false") +
			",\"policyRolloutMode\":\"" +
			s.emailPolicyRolloutMode +
			"\",\"policyEnforceChannel\":\"" +
			s.emailPolicyEnforceChannel +
			"\",\"policyCanaryEligible\":" +
			std::string(s.emailPolicyCanaryEligible ? "true" : "false") +
			",\"rollbackBridgeEnabled\":" +
			std::string(s.emailRollbackBridgeEnabled ? "true" : "false") +
			",\"resolvedPolicyId\":\"" +
			s.emailResolvedPolicyId +
			"\",\"resolvedBackends\":[\"" +
			(s.emailResolvedBackends.empty()
				? std::string("himalaya\",\"imap-smtp-email")
				: [&]() {
					std::string list;
					for (std::size_t i = 0; i < s.emailResolvedBackends.size(); ++i)
					{
						if (i > 0)
						{
							list += "\",\"";
						}
						list += s.emailResolvedBackends[i];
					}
					return list;
				}()) +
			"\"],\"policyActions\":{\"unavailable\":\"" +
					s.emailPolicyActionUnavailable +
					"\",\"authError\":\"" +
					s.emailPolicyActionAuthError +
					"\",\"execError\":\"" +
					s.emailPolicyActionExecError +
					"\"},\"retryMaxAttempts\":" +
					std::to_string(s.emailRetryMaxAttempts) +
					",\"retryDelayMs\":" +
					std::to_string(s.emailRetryDelayMs) +
					",\"requiresApproval\":" +
					std::string(s.emailRequiresApproval ? "true" : "false") +
					",\"approvalTokenTtlMinutes\":" +
					std::to_string(s.emailApprovalTokenTtlMinutes) +
					",\"capabilityState\":\"" +
					s.emailCapabilityState +
					"\",\"healthGeneratedAtEpochMs\":" +
					std::to_string(s.emailHealthGeneratedAtEpochMs) +
					",\"healthTtlMs\":" +
					std::to_string(s.emailHealthTtlMs) +
					",\"probeReadyCount\":" +
					std::to_string(s.emailProbeReadyCount) +
					",\"probeUnavailableCount\":" +
					std::to_string(s.emailProbeUnavailableCount) +
					",\"fallbackAttempts\":" +
					std::to_string(s.emailFallbackAttempts) +
					",\"fallbackSuccess\":" +
					std::to_string(s.emailFallbackSuccess) +
					",\"fallbackFailure\":" +
					std::to_string(s.emailFallbackFailure) +
					"}," +
					"\"agents\":{\"count\":" + std::to_string(s.agentsCount) +
					",\"defaultAgent\":\"" + s.agentsDefaultAgent + "\"}," +
					"\"subagents\":{\"active\":" + std::to_string(s.subagentsActive) +
					",\"pendingAnnounce\":" + std::to_string(s.subagentsPendingAnnounce) + "}," +
					"\"acp\":{\"lastAllowed\":" +
					std::string(s.acpLastAllowed ? "true" : "false") +
					",\"reason\":\"" + s.acpReason + "\"}," +
					"\"embedded\":{\"activeRuns\":" + std::to_string(s.embeddedActiveRuns) +
					",\"dynamicLoopEnabled\":" +
					std::string(s.embeddedDynamicLoopEnabled ? "true" : "false") +
					",\"canaryEligible\":" +
					std::string(s.embeddedCanaryEligible ? "true" : "false") +
					",\"promotionReady\":" +
					std::string(s.embeddedPromotionReady ? "true" : "false") +
					",\"promotionMinRuns\":" +
					std::to_string(s.embeddedPromotionMinRuns) +
					",\"promotionMinSuccessRate\":" +
					std::to_string(s.embeddedPromotionMinSuccessRate) +
					",\"fallbackUsed\":" +
					std::string(s.embeddedFallbackUsed ? "true" : "false") +
					",\"fallbackReason\":\"" + s.embeddedFallbackReason +
					"\",\"totalRuns\":" + std::to_string(s.embeddedTotalRuns) +
					",\"successRate\":" + std::to_string(s.embeddedSuccessRate) +
					",\"runSuccess\":" + std::to_string(s.embeddedRunSuccess) +
					",\"runFailure\":" + std::to_string(s.embeddedRunFailure) +
					",\"runTimeout\":" + std::to_string(s.embeddedRunTimeout) +
					",\"runCancelled\":" + std::to_string(s.embeddedRunCancelled) +
					",\"runFallback\":" + std::to_string(s.embeddedRunFallback) +
					",\"taskDeltaTransitions\":" +
					std::to_string(s.embeddedTaskDeltaTransitions) + "}," +
					"\"tools\":{\"policyEntries\":" + std::to_string(s.toolsPolicyEntries) +
					",\"shellProcesses\":" + std::to_string(s.toolsShellProcesses) + "}," +
					"\"modelAuth\":{\"primary\":\"" + s.modelPrimary +
					"\",\"fallback\":\"" + s.modelFallback +
					"\",\"failovers\":" + std::to_string(s.modelFailovers) +
					",\"authProfiles\":" + std::to_string(s.authProfiles) + "}," +
					"\"sandbox\":{\"enabledCount\":" + std::to_string(s.sandboxEnabledCount) +
					",\"browserEnabledCount\":" + std::to_string(s.sandboxBrowserEnabledCount) + "}," +
					"\"embeddings\":{\"enabled\":" +
					std::string(s.embeddingsEnabled ? "true" : "false") +
					",\"ready\":" + std::string(s.embeddingsReady ? "true" : "false") +
					",\"provider\":\"" + s.embeddingsProvider +
					"\",\"status\":\"" + s.embeddingsStatus +
					"\",\"dimension\":" + std::to_string(s.embeddingsDimension) +
					",\"maxSequenceLength\":" + std::to_string(s.embeddingsMaxSequenceLength) +
					",\"modelPathConfigured\":" +
					std::string(s.embeddingsModelPathConfigured ? "true" : "false") +
					",\"tokenizerPathConfigured\":" +
					std::string(s.embeddingsTokenizerPathConfigured ? "true" : "false") +
					",\"configFeatureImplemented\":" +
					std::string(s.embeddingsConfigFeatureImplemented ? "true" : "false") + "}," +
					"\"localModel\":{\"enabled\":" +
					std::string(s.localModelEnabled ? "true" : "false") +
					",\"ready\":" + std::string(s.localModelReady ? "true" : "false") +
					",\"rolloutEligible\":" +
					std::string(s.localModelRolloutEligible ? "true" : "false") +
					",\"activationEnabled\":" +
					std::string(s.localModelActivationEnabled ? "true" : "false") +
					",\"activationReason\":\"" + s.localModelActivationReason +
					",\"provider\":\"" + s.localModelProvider +
					"\",\"rolloutStage\":\"" + s.localModelRolloutStage +
					"\",\"storageRoot\":\"" + s.localModelStorageRoot +
					"\",\"version\":\"" + s.localModelVersion +
					"\",\"status\":\"" + s.localModelStatus +
					"\",\"verboseMetrics\":" +
					std::string(s.localModelVerboseMetrics ? "true" : "false") +
					",\"runtimeDllPresent\":" +
					std::string(s.localModelRuntimeDllPresent ? "true" : "false") +
					",\"maxTokens\":" + std::to_string(s.localModelMaxTokens) +
					",\"temperature\":" + std::to_string(s.localModelTemperature) +
					",\"modelLoadAttempts\":" + std::to_string(s.localModelModelLoadAttempts) +
					",\"modelLoadFailures\":" + std::to_string(s.localModelModelLoadFailures) +
					",\"requestsStarted\":" + std::to_string(s.localModelRequestsStarted) +
					",\"requestsCompleted\":" + std::to_string(s.localModelRequestsCompleted) +
					",\"requestsFailed\":" + std::to_string(s.localModelRequestsFailed) +
					",\"requestsCancelled\":" + std::to_string(s.localModelRequestsCancelled) +
					",\"cumulativeTokens\":" + std::to_string(s.localModelCumulativeTokens) +
					",\"cumulativeLatencyMs\":" + std::to_string(s.localModelCumulativeLatencyMs) +
					",\"lastLatencyMs\":" + std::to_string(s.localModelLastLatencyMs) +
					",\"lastGeneratedTokens\":" + std::to_string(s.localModelLastGeneratedTokens) +
					",\"lastTokensPerSecond\":" + std::to_string(s.localModelLastTokensPerSecond) +
					",\"modelPathConfigured\":" +
					std::string(s.localModelModelPathConfigured ? "true" : "false") +
					",\"modelHashConfigured\":" +
					std::string(s.localModelModelHashConfigured ? "true" : "false") +
					",\"modelHashVerified\":" +
					std::string(s.localModelModelHashVerified ? "true" : "false") +
					",\"tokenizerPathConfigured\":" +
					std::string(s.localModelTokenizerPathConfigured ? "true" : "false") +
					",\"tokenizerHashConfigured\":" +
					std::string(s.localModelTokenizerHashConfigured ? "true" : "false") +
					",\"tokenizerHashVerified\":" +
					std::string(s.localModelTokenizerHashVerified ? "true" : "false") +
					"}," +
					"\"retrieval\":{\"enabled\":" +
					std::string(s.retrievalEnabled ? "true" : "false") +
					",\"recordCount\":" + std::to_string(s.retrievalRecordCount) +
					",\"lastQueryCount\":" + std::to_string(s.retrievalLastQueryCount) +
					",\"status\":\"" + s.retrievalStatus + "\"}," +
					"\"skills\":{\"catalogEntries\":" + std::to_string(s.skillsCatalogEntries) +
					",\"promptIncluded\":" + std::to_string(s.skillsPromptIncluded) +
					",\"selfEvolvingReminderInjected\":" +
					std::string(s.skillsSelfEvolvingReminderInjected ? "true" : "false") + "}," +
					"\"hooks\":{\"loaded\":" + std::to_string(s.hooksLoaded) +
					",\"engineMode\":\"" + s.hooksEngineMode + "\"" +
					",\"hookEngineEnabled\":" +
					std::string(s.hooksEngineEnabled ? "true" : "false") +
					",\"fallbackPromptInjection\":" +
					std::string(s.hooksFallbackPromptInjection ? "true" : "false") +
					",\"reminderEnabled\":" +
					std::string(s.hooksReminderEnabled ? "true" : "false") +
					",\"reminderVerbosity\":\"" + s.hooksReminderVerbosity + "\"" +
					",\"strictPolicyEnforcement\":" +
					std::string(s.hooksStrictPolicyEnforcement ? "true" : "false") +
					",\"allowedPackagesCount\":" + std::to_string(s.hooksAllowedPackagesCount) +
					",\"governanceReportingEnabled\":" +
					std::string(s.hooksGovernanceReportingEnabled ? "true" : "false") +
					",\"governanceReportsGenerated\":" + std::to_string(s.hooksGovernanceReportsGenerated) +
					",\"lastGovernanceReportPath\":\"" + s.hooksLastGovernanceReportPath + "\"" +
					",\"autoRemediationEnabled\":" +
					std::string(s.hooksAutoRemediationEnabled ? "true" : "false") +
					",\"autoRemediationRequiresApproval\":" +
					std::string(s.hooksAutoRemediationRequiresApproval ? "true" : "false") +
					",\"autoRemediationExecuted\":" + std::to_string(s.hooksAutoRemediationExecuted) +
					",\"lastAutoRemediationStatus\":\"" + s.hooksLastAutoRemediationStatus + "\"" +
					",\"autoRemediationTenantId\":\"" + s.hooksAutoRemediationTenantId + "\"" +
					",\"lastAutoRemediationPlaybookPath\":\"" + s.hooksLastAutoRemediationPlaybookPath + "\"" +
					",\"autoRemediationTokenMaxAgeMinutes\":" +
					std::to_string(s.hooksAutoRemediationTokenMaxAgeMinutes) +
					",\"autoRemediationTokenRotations\":" +
					std::to_string(s.hooksAutoRemediationTokenRotations) +
					",\"remediationTelemetryEnabled\":" +
					std::string(s.hooksRemediationTelemetryEnabled ? "true" : "false") +
					",\"remediationAuditEnabled\":" +
					std::string(s.hooksRemediationAuditEnabled ? "true" : "false") +
					",\"lastRemediationTelemetryPath\":\"" + s.hooksLastRemediationTelemetryPath + "\"" +
					",\"lastRemediationAuditPath\":\"" + s.hooksLastRemediationAuditPath + "\"" +
					",\"remediationSloStatus\":\"" + s.hooksRemediationSloStatus + "\"" +
					",\"remediationSloMaxDriftDetected\":" + std::to_string(s.hooksRemediationSloMaxDriftDetected) +
					",\"remediationSloMaxPolicyBlocked\":" + std::to_string(s.hooksRemediationSloMaxPolicyBlocked) +
					",\"complianceAttestationEnabled\":" +
					std::string(s.hooksComplianceAttestationEnabled ? "true" : "false") +
					",\"lastComplianceAttestationPath\":\"" + s.hooksLastComplianceAttestationPath + "\"" +
					",\"enterpriseSlaGovernanceEnabled\":" +
					std::string(s.hooksEnterpriseSlaGovernanceEnabled ? "true" : "false") +
					",\"enterpriseSlaPolicyId\":\"" + s.hooksEnterpriseSlaPolicyId + "\"" +
					",\"crossTenantAttestationAggregationEnabled\":" +
					std::string(s.hooksCrossTenantAttestationAggregationEnabled ? "true" : "false") +
					",\"crossTenantAttestationAggregationStatus\":\"" + s.hooksCrossTenantAttestationAggregationStatus + "\"" +
					",\"crossTenantAttestationAggregationCount\":" + std::to_string(s.hooksCrossTenantAttestationAggregationCount) +
					",\"lastCrossTenantAttestationAggregationPath\":\"" + s.hooksLastCrossTenantAttestationAggregationPath + "\"" +
					",\"selfEvolvingHookTriggered\":" +
					std::string(s.hooksSelfEvolvingHookTriggered ? "true" : "false") +
					",\"invalidMetadata\":" + std::to_string(s.hooksInvalidMetadata) +
					",\"unsafeHandlerPaths\":" + std::to_string(s.hooksUnsafeHandlerPaths) +
					",\"missingHandlers\":" + std::to_string(s.hooksMissingHandlers) +
					",\"eventsEmitted\":" + std::to_string(s.hooksEventsEmitted) +
					",\"eventValidationFailed\":" + std::to_string(s.hooksEventValidationFailed) +
					",\"eventsDropped\":" + std::to_string(s.hooksEventsDropped) +
					",\"dispatches\":" + std::to_string(s.hooksDispatches) +
					",\"hookDispatchCount\":" + std::to_string(s.hooksHookDispatchCount) +
					",\"dispatchSuccess\":" + std::to_string(s.hooksDispatchSuccess) +
					",\"dispatchFailures\":" + std::to_string(s.hooksDispatchFailures) +
					",\"hookFailureCount\":" + std::to_string(s.hooksHookFailureCount) +
					",\"dispatchSkipped\":" + std::to_string(s.hooksDispatchSkipped) +
					",\"dispatchTimeouts\":" + std::to_string(s.hooksDispatchTimeouts) +
					",\"guardRejected\":" + std::to_string(s.hooksGuardRejected) +
					",\"reminderTriggered\":" + std::to_string(s.hooksReminderTriggered) +
					",\"reminderInjected\":" + std::to_string(s.hooksReminderInjected) +
					",\"reminderSkipped\":" + std::to_string(s.hooksReminderSkipped) +
					",\"policyBlocked\":" + std::to_string(s.hooksPolicyBlocked) +
					",\"driftDetected\":" + std::to_string(s.hooksDriftDetected) +
					",\"lastDriftReason\":\"" + s.hooksLastDriftReason + "\"" +
					",\"reminderState\":\"" + s.hooksReminderState + "\"" +
					",\"reminderReason\":\"" + s.hooksReminderReason + "\"" +
					"}," +
					"\"features\":{\"implemented\":" + std::to_string(s.featuresImplemented) +
					",\"inProgress\":" + std::to_string(s.featuresInProgress) +
					",\"planned\":" + std::to_string(s.featuresPlanned) +
					",\"registryState\":\"" + s.featuresRegistryState + "\"}}";

				return report;
	}

} // namespace blazeclaw::core
