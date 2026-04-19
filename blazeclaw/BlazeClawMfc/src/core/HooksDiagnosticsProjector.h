#pragma once

#include "diagnostics/DiagnosticsSnapshot.h"
#include "SkillsCatalogService.h"
#include "HookCatalogService.h"
#include "HookEventService.h"
#include "HookExecutionService.h"

#include <string>

namespace blazeclaw::core {

    class HooksDiagnosticsProjector {
    public:
        struct Context {
            bool engineEnabled = false;
            bool fallbackPromptInjection = false;
            bool reminderEnabled = false;
            std::wstring reminderVerbosity;
            bool strictPolicyEnforcement = false;
            std::size_t allowedPackagesCount = 0;
            bool governanceReportingEnabled = false;
            std::uint64_t governanceReportsGenerated = 0;
            std::wstring lastGovernanceReportPath;
            bool autoRemediationEnabled = false;
            bool autoRemediationRequiresApproval = false;
            std::uint64_t autoRemediationExecuted = 0;
            std::wstring lastAutoRemediationStatus;
            std::wstring autoRemediationTenantId;
            std::wstring lastAutoRemediationPlaybookPath;
            std::uint32_t autoRemediationTokenMaxAgeMinutes = 0;
            std::uint64_t autoRemediationTokenRotations = 0;
            bool remediationTelemetryEnabled = false;
            bool remediationAuditEnabled = false;
            std::wstring lastRemediationTelemetryPath;
            std::wstring lastRemediationAuditPath;
            std::wstring remediationSloStatus;
            std::uint32_t remediationSloMaxDriftDetected = 0;
            std::uint32_t remediationSloMaxPolicyBlocked = 0;
            bool complianceAttestationEnabled = false;
            std::wstring lastComplianceAttestationPath;
            bool enterpriseSlaGovernanceEnabled = false;
            std::wstring enterpriseSlaPolicyId;
            bool crossTenantAttestationAggregationEnabled = false;
            std::wstring crossTenantAttestationAggregationStatus;
            std::uint64_t crossTenantAttestationAggregationCount = 0;
            std::wstring lastCrossTenantAttestationAggregationPath;
            bool selfEvolvingHookTriggered = false;
            const HookCatalogSnapshot* hookCatalog = nullptr;
            const HookEventSnapshot* hookEvents = nullptr;
            const HookExecutionSnapshot* hookExecution = nullptr;
        };

        void Apply(
            const Context& context,
            DiagnosticsSnapshot& snapshot) const {
            snapshot.hooksEngineEnabled = context.engineEnabled;
            snapshot.hooksFallbackPromptInjection = context.fallbackPromptInjection;
            snapshot.hooksReminderEnabled = context.reminderEnabled;
            snapshot.hooksReminderVerbosity = ToNarrowAscii(context.reminderVerbosity);
            snapshot.hooksStrictPolicyEnforcement =
                context.strictPolicyEnforcement;
            snapshot.hooksAllowedPackagesCount = context.allowedPackagesCount;
            snapshot.hooksGovernanceReportingEnabled =
                context.governanceReportingEnabled;
            snapshot.hooksGovernanceReportsGenerated =
                context.governanceReportsGenerated;
            snapshot.hooksLastGovernanceReportPath =
                ToNarrowAscii(context.lastGovernanceReportPath);
            snapshot.hooksAutoRemediationEnabled = context.autoRemediationEnabled;
            snapshot.hooksAutoRemediationRequiresApproval =
                context.autoRemediationRequiresApproval;
            snapshot.hooksAutoRemediationExecuted =
                context.autoRemediationExecuted;
            snapshot.hooksLastAutoRemediationStatus =
                ToNarrowAscii(context.lastAutoRemediationStatus);
            snapshot.hooksAutoRemediationTenantId =
                ToNarrowAscii(context.autoRemediationTenantId);
            snapshot.hooksLastAutoRemediationPlaybookPath =
                ToNarrowAscii(context.lastAutoRemediationPlaybookPath);
            snapshot.hooksAutoRemediationTokenMaxAgeMinutes =
                context.autoRemediationTokenMaxAgeMinutes;
            snapshot.hooksAutoRemediationTokenRotations =
                context.autoRemediationTokenRotations;
            snapshot.hooksRemediationTelemetryEnabled =
                context.remediationTelemetryEnabled;
            snapshot.hooksRemediationAuditEnabled =
                context.remediationAuditEnabled;
            snapshot.hooksLastRemediationTelemetryPath =
                ToNarrowAscii(context.lastRemediationTelemetryPath);
            snapshot.hooksLastRemediationAuditPath =
                ToNarrowAscii(context.lastRemediationAuditPath);
            snapshot.hooksRemediationSloStatus =
                ToNarrowAscii(context.remediationSloStatus);
            snapshot.hooksRemediationSloMaxDriftDetected =
                context.remediationSloMaxDriftDetected;
            snapshot.hooksRemediationSloMaxPolicyBlocked =
                context.remediationSloMaxPolicyBlocked;
            snapshot.hooksComplianceAttestationEnabled =
                context.complianceAttestationEnabled;
            snapshot.hooksLastComplianceAttestationPath =
                ToNarrowAscii(context.lastComplianceAttestationPath);
            snapshot.hooksEnterpriseSlaGovernanceEnabled =
                context.enterpriseSlaGovernanceEnabled;
            snapshot.hooksEnterpriseSlaPolicyId =
                ToNarrowAscii(context.enterpriseSlaPolicyId);
            snapshot.hooksCrossTenantAttestationAggregationEnabled =
                context.crossTenantAttestationAggregationEnabled;
            snapshot.hooksCrossTenantAttestationAggregationStatus =
                ToNarrowAscii(context.crossTenantAttestationAggregationStatus);
            snapshot.hooksCrossTenantAttestationAggregationCount =
                context.crossTenantAttestationAggregationCount;
            snapshot.hooksLastCrossTenantAttestationAggregationPath =
                ToNarrowAscii(context.lastCrossTenantAttestationAggregationPath);
            snapshot.hooksSelfEvolvingHookTriggered =
                context.selfEvolvingHookTriggered;

            if (context.hookCatalog != nullptr) {
                snapshot.hooksLoaded = context.hookCatalog->diagnostics.hooksLoaded;
                snapshot.hooksInvalidMetadata =
                    context.hookCatalog->diagnostics.invalidMetadataFiles;
                snapshot.hooksUnsafeHandlerPaths =
                    context.hookCatalog->diagnostics.unsafeHandlerPaths;
                snapshot.hooksMissingHandlers =
                    context.hookCatalog->diagnostics.missingHandlerFiles;
            }

            if (context.hookEvents != nullptr) {
                snapshot.hooksEventsEmitted =
                    context.hookEvents->diagnostics.emittedCount;
                snapshot.hooksEventValidationFailed =
                    context.hookEvents->diagnostics.validationFailedCount;
                snapshot.hooksEventsDropped =
                    context.hookEvents->diagnostics.droppedCount;
            }

            if (context.hookExecution != nullptr) {
                snapshot.hooksEngineMode =
                    ToNarrowAscii(context.hookExecution->diagnostics.engineMode);
                snapshot.hooksDispatches =
                    context.hookExecution->diagnostics.dispatchCount;
                snapshot.hooksHookDispatchCount =
                    context.hookExecution->diagnostics.dispatchCount;
                snapshot.hooksDispatchSuccess =
                    context.hookExecution->diagnostics.successCount;
                snapshot.hooksDispatchFailures =
                    context.hookExecution->diagnostics.failureCount;
                snapshot.hooksHookFailureCount =
                    context.hookExecution->diagnostics.failureCount;
                snapshot.hooksDispatchSkipped =
                    context.hookExecution->diagnostics.skippedCount;
                snapshot.hooksDispatchTimeouts =
                    context.hookExecution->diagnostics.timeoutCount;
                snapshot.hooksGuardRejected =
                    context.hookExecution->diagnostics.guardRejectedCount;
                snapshot.hooksReminderTriggered =
                    context.hookExecution->diagnostics.reminderTriggeredCount;
                snapshot.hooksReminderInjected =
                    context.hookExecution->diagnostics.reminderInjectedCount;
                snapshot.hooksReminderSkipped =
                    context.hookExecution->diagnostics.reminderSkippedCount;
                snapshot.hooksPolicyBlocked =
                    context.hookExecution->diagnostics.policyBlockedCount;
                snapshot.hooksDriftDetected =
                    context.hookExecution->diagnostics.driftDetectedCount;
                snapshot.hooksLastDriftReason =
                    ToNarrowAscii(context.hookExecution->diagnostics.lastDriftReason);
                snapshot.hooksReminderState =
                    ToNarrowAscii(context.hookExecution->diagnostics.lastReminderState);
                snapshot.hooksReminderReason =
                    ToNarrowAscii(context.hookExecution->diagnostics.lastReminderReason);
            }
        }

    private:
        [[nodiscard]] static std::string ToNarrowAscii(
            const std::wstring& value) {
            std::string output;
            output.reserve(value.size());
            for (const auto ch : value) {
                output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
            }

            return output;
        }
    };

} // namespace blazeclaw::core
