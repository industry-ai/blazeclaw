#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct DiagnosticsSnapshot {
		bool runtimeRunning = false;
		std::string gatewayWarning;
		std::string gatewayStartupMode;
		std::string gatewayStartupModeSource;
		std::string gatewayStartupFailedStage;
		bool gatewayStartupDegraded = false;
		bool gatewayManagedConfigReloaderStarted = false;
		bool gatewayManagedConfigReloaderRunning = false;
		bool gatewayClosePreludeExecuted = false;
		bool gatewayRuntimeStateCreated = false;
		bool gatewayRuntimeServicesStarted = false;
		bool gatewayTransportHandlersAttached = false;
		bool gatewayRuntimeSubscriptionsStarted = false;
		std::string gatewayManagedConfigPath;
		std::uint64_t gatewayManagedConfigApplyCount = 0;
		std::uint64_t gatewayManagedConfigRejectCount = 0;
		std::uint64_t gatewayAuthSessionGenerationCurrent = 0;
		std::uint64_t gatewayAuthSessionGenerationRequired = 0;
		std::uint64_t gatewayAuthSessionGenerationRejectCount = 0;

		bool emailPreflightEnabled = false;
		bool emailPolicyProfilesEnabled = false;
		bool emailPolicyProfilesEnforce = false;
		bool emailPolicyProfilesRuntimeEnabled = false;
		bool emailPolicyProfilesRuntimeEnforce = false;
		std::string emailPolicyRolloutMode;
		std::string emailPolicyEnforceChannel;
		bool emailPolicyCanaryEligible = false;
		bool emailRollbackBridgeEnabled = false;
		std::string emailResolvedPolicyId;
		std::vector<std::string> emailResolvedBackends;
		std::string emailPolicyActionUnavailable;
		std::string emailPolicyActionAuthError;
		std::string emailPolicyActionExecError;
		std::uint32_t emailRetryMaxAttempts = 0;
		std::uint32_t emailRetryDelayMs = 0;
		bool emailRequiresApproval = false;
		std::uint32_t emailApprovalTokenTtlMinutes = 0;
		std::string emailCapabilityState;
		std::uint64_t emailHealthGeneratedAtEpochMs = 0;
		std::uint64_t emailHealthTtlMs = 0;
		std::size_t emailProbeReadyCount = 0;
		std::size_t emailProbeUnavailableCount = 0;
		std::uint64_t emailFallbackAttempts = 0;
		std::uint64_t emailFallbackSuccess = 0;
		std::uint64_t emailFallbackFailure = 0;

		std::size_t agentsCount = 0;
		std::string agentsDefaultAgent;
		std::size_t subagentsActive = 0;
		std::size_t subagentsPendingAnnounce = 0;
		bool acpLastAllowed = false;
		std::string acpReason;

		std::size_t embeddedActiveRuns = 0;
		bool embeddedDynamicLoopEnabled = false;
		bool embeddedCanaryEligible = false;
		bool embeddedPromotionReady = false;
		std::uint64_t embeddedPromotionMinRuns = 0;
		double embeddedPromotionMinSuccessRate = 0.0;
		bool embeddedFallbackUsed = false;
		std::string embeddedFallbackReason;
		std::uint64_t embeddedTotalRuns = 0;
		double embeddedSuccessRate = 0.0;
		std::uint64_t embeddedRunSuccess = 0;
		std::uint64_t embeddedRunFailure = 0;
		std::uint64_t embeddedRunTimeout = 0;
		std::uint64_t embeddedRunCancelled = 0;
		std::uint64_t embeddedRunFallback = 0;
		std::uint64_t embeddedTaskDeltaTransitions = 0;

		std::size_t toolsPolicyEntries = 0;
		std::size_t toolsShellProcesses = 0;

		std::string modelPrimary;
		std::string modelFallback;
		std::size_t modelFailovers = 0;
		std::size_t authProfiles = 0;

		std::size_t sandboxEnabledCount = 0;
		std::size_t sandboxBrowserEnabledCount = 0;

		bool embeddingsEnabled = false;
		bool embeddingsReady = false;
		std::string embeddingsProvider;
		std::string embeddingsStatus;
		std::size_t embeddingsDimension = 0;
		std::size_t embeddingsMaxSequenceLength = 0;
		bool embeddingsModelPathConfigured = false;
		bool embeddingsTokenizerPathConfigured = false;
		bool embeddingsConfigFeatureImplemented = false;

		bool localModelEnabled = false;
		bool localModelReady = false;
		bool localModelRolloutEligible = false;
		bool localModelActivationEnabled = false;
		std::string localModelActivationReason;
		std::string localModelProvider;
		std::string localModelRolloutStage;
		std::string localModelStorageRoot;
		std::string localModelVersion;
		std::string localModelStatus;
		bool localModelVerboseMetrics = false;
		bool localModelRuntimeDllPresent = false;
		std::uint32_t localModelMaxTokens = 0;
		double localModelTemperature = 0.0;
		std::uint64_t localModelModelLoadAttempts = 0;
		std::uint64_t localModelModelLoadFailures = 0;
		std::uint64_t localModelRequestsStarted = 0;
		std::uint64_t localModelRequestsCompleted = 0;
		std::uint64_t localModelRequestsFailed = 0;
		std::uint64_t localModelRequestsCancelled = 0;
		std::uint64_t localModelCumulativeTokens = 0;
		std::uint64_t localModelCumulativeLatencyMs = 0;
		std::uint32_t localModelLastLatencyMs = 0;
		std::uint32_t localModelLastGeneratedTokens = 0;
		double localModelLastTokensPerSecond = 0.0;
		bool localModelModelPathConfigured = false;
		bool localModelModelHashConfigured = false;
		bool localModelModelHashVerified = false;
		bool localModelTokenizerPathConfigured = false;
		bool localModelTokenizerHashConfigured = false;
		bool localModelTokenizerHashVerified = false;

		bool retrievalEnabled = false;
		std::size_t retrievalRecordCount = 0;
		std::size_t retrievalLastQueryCount = 0;
		std::string retrievalStatus;

		std::size_t skillsCatalogEntries = 0;
		std::size_t skillsPromptIncluded = 0;
		bool skillsSelfEvolvingReminderInjected = false;

		std::size_t hooksLoaded = 0;
		std::string hooksEngineMode;
		bool hooksEngineEnabled = false;
		bool hooksFallbackPromptInjection = false;
		bool hooksReminderEnabled = false;
		std::string hooksReminderVerbosity;
		bool hooksStrictPolicyEnforcement = false;
		std::size_t hooksAllowedPackagesCount = 0;
		bool hooksGovernanceReportingEnabled = false;
		std::uint64_t hooksGovernanceReportsGenerated = 0;
		std::string hooksLastGovernanceReportPath;
		bool hooksAutoRemediationEnabled = false;
		bool hooksAutoRemediationRequiresApproval = false;
		std::uint64_t hooksAutoRemediationExecuted = 0;
		std::string hooksLastAutoRemediationStatus;
		std::string hooksAutoRemediationTenantId;
		std::string hooksLastAutoRemediationPlaybookPath;
		std::uint32_t hooksAutoRemediationTokenMaxAgeMinutes = 0;
		std::uint64_t hooksAutoRemediationTokenRotations = 0;
		bool hooksRemediationTelemetryEnabled = false;
		bool hooksRemediationAuditEnabled = false;
		std::string hooksLastRemediationTelemetryPath;
		std::string hooksLastRemediationAuditPath;
		std::string hooksRemediationSloStatus;
		std::uint32_t hooksRemediationSloMaxDriftDetected = 0;
		std::uint32_t hooksRemediationSloMaxPolicyBlocked = 0;
		bool hooksComplianceAttestationEnabled = false;
		std::string hooksLastComplianceAttestationPath;
		bool hooksEnterpriseSlaGovernanceEnabled = false;
		std::string hooksEnterpriseSlaPolicyId;
		bool hooksCrossTenantAttestationAggregationEnabled = false;
		std::string hooksCrossTenantAttestationAggregationStatus;
		std::uint64_t hooksCrossTenantAttestationAggregationCount = 0;
		std::string hooksLastCrossTenantAttestationAggregationPath;
		bool hooksSelfEvolvingHookTriggered = false;
		std::size_t hooksInvalidMetadata = 0;
		std::size_t hooksUnsafeHandlerPaths = 0;
		std::size_t hooksMissingHandlers = 0;
		std::uint64_t hooksEventsEmitted = 0;
		std::uint64_t hooksEventValidationFailed = 0;
		std::uint64_t hooksEventsDropped = 0;
		std::uint64_t hooksDispatches = 0;
		std::uint64_t hooksHookDispatchCount = 0;
		std::uint64_t hooksDispatchSuccess = 0;
		std::uint64_t hooksDispatchFailures = 0;
		std::uint64_t hooksHookFailureCount = 0;
		std::uint64_t hooksDispatchSkipped = 0;
		std::uint64_t hooksDispatchTimeouts = 0;
		std::uint64_t hooksGuardRejected = 0;
		std::uint64_t hooksReminderTriggered = 0;
		std::uint64_t hooksReminderInjected = 0;
		std::uint64_t hooksReminderSkipped = 0;
		std::uint64_t hooksPolicyBlocked = 0;
		std::uint64_t hooksDriftDetected = 0;
		std::string hooksLastDriftReason;
		std::string hooksReminderState;
		std::string hooksReminderReason;

		std::size_t featuresImplemented = 0;
		std::size_t featuresInProgress = 0;
		std::size_t featuresPlanned = 0;
		std::string featuresRegistryState;
	};

} // namespace blazeclaw::core
