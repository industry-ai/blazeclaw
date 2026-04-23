#pragma once

#include "diagnostics/DiagnosticsSnapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blazeclaw::core {

    class GatewayLifecycleDiagnosticsProjector {
    public:
        struct Context {
            bool runtimeRunning = false;
            std::string gatewayWarning;
            std::string startupMode;
            std::string startupModeSource;
            std::string startupFailedStage;
            bool startupDegraded = false;
            bool managedConfigReloaderStarted = false;
            bool managedConfigReloaderRunning = false;
            bool closePreludeExecuted = false;
            bool startupFailureCleanupExecuted = false;
            std::string cleanupPath;
            bool runtimeStateCreated = false;
            bool runtimeServicesStarted = false;
            bool transportHandlersAttached = false;
            bool runtimeSubscriptionsStarted = false;
            std::string managedConfigPath;
            std::uint64_t managedConfigApplyCount = 0;
            std::uint64_t managedConfigRejectCount = 0;
            std::uint64_t authSessionGenerationCurrent = 0;
            std::uint64_t authSessionGenerationRequired = 0;
            std::uint64_t authSessionGenerationRejectCount = 0;
            std::vector<std::string> transitions;
        };

        void Apply(
            const Context& context,
            DiagnosticsSnapshot& snapshot) const {
            snapshot.runtimeRunning = context.runtimeRunning;
            snapshot.gatewayWarning = context.gatewayWarning;
            snapshot.gatewayStartupMode = context.startupMode;
            snapshot.gatewayStartupModeSource = context.startupModeSource;
            snapshot.gatewayStartupFailedStage = context.startupFailedStage;
            snapshot.gatewayStartupDegraded = context.startupDegraded;
            snapshot.gatewayManagedConfigReloaderStarted =
                context.managedConfigReloaderStarted;
            snapshot.gatewayManagedConfigReloaderRunning =
                context.managedConfigReloaderRunning;
            snapshot.gatewayClosePreludeExecuted = context.closePreludeExecuted;
            snapshot.gatewayStartupFailureCleanupExecuted =
                context.startupFailureCleanupExecuted;
            snapshot.gatewayCleanupPath = context.cleanupPath;
            snapshot.gatewayRuntimeStateCreated = context.runtimeStateCreated;
            snapshot.gatewayRuntimeServicesStarted = context.runtimeServicesStarted;
            snapshot.gatewayTransportHandlersAttached =
                context.transportHandlersAttached;
            snapshot.gatewayRuntimeSubscriptionsStarted =
                context.runtimeSubscriptionsStarted;
            snapshot.gatewayManagedConfigPath = context.managedConfigPath;
            snapshot.gatewayManagedConfigApplyCount = context.managedConfigApplyCount;
            snapshot.gatewayManagedConfigRejectCount =
                context.managedConfigRejectCount;
            snapshot.gatewayAuthSessionGenerationCurrent =
                context.authSessionGenerationCurrent;
            snapshot.gatewayAuthSessionGenerationRequired =
                context.authSessionGenerationRequired;
            snapshot.gatewayAuthSessionGenerationRejectCount =
                context.authSessionGenerationRejectCount;
            snapshot.gatewayLifecycleTransitions = context.transitions;

            snapshot.gatewayParityLifecycle.schemaVersion =
                GatewayParityLifecycleContract::kSchemaVersion;
            snapshot.gatewayParityLifecycle.startupMode = context.startupMode;
            snapshot.gatewayParityLifecycle.startupModeSource = context.startupModeSource;
            snapshot.gatewayParityLifecycle.failedStage = context.startupFailedStage;
            snapshot.gatewayParityLifecycle.startupDegraded = context.startupDegraded;
            snapshot.gatewayParityLifecycle.runtimeRunning = context.runtimeRunning;
            snapshot.gatewayParityLifecycle.managedConfigReloaderStarted =
                context.managedConfigReloaderStarted;
            snapshot.gatewayParityLifecycle.managedConfigReloaderRunning =
                context.managedConfigReloaderRunning;
            snapshot.gatewayParityLifecycle.closePreludeExecuted = context.closePreludeExecuted;
            snapshot.gatewayParityLifecycle.startupFailureCleanupExecuted =
                context.startupFailureCleanupExecuted;
            snapshot.gatewayParityLifecycle.cleanupPath = context.cleanupPath;
            snapshot.gatewayParityLifecycle.runtimeStateCreated = context.runtimeStateCreated;
            snapshot.gatewayParityLifecycle.runtimeServicesStarted = context.runtimeServicesStarted;
            snapshot.gatewayParityLifecycle.transportHandlersAttached =
                context.transportHandlersAttached;
            snapshot.gatewayParityLifecycle.runtimeSubscriptionsStarted =
                context.runtimeSubscriptionsStarted;
            snapshot.gatewayParityLifecycle.managedConfigPath = context.managedConfigPath;
            snapshot.gatewayParityLifecycle.managedConfigApplyCount = context.managedConfigApplyCount;
            snapshot.gatewayParityLifecycle.managedConfigRejectCount =
                context.managedConfigRejectCount;
            snapshot.gatewayParityLifecycle.authSessionGenerationCurrent =
                context.authSessionGenerationCurrent;
            snapshot.gatewayParityLifecycle.authSessionGenerationRequired =
                context.authSessionGenerationRequired;
            snapshot.gatewayParityLifecycle.authSessionGenerationRejectCount =
                context.authSessionGenerationRejectCount;
            snapshot.gatewayParityLifecycle.transitionTrace = context.transitions;
        }
    };

} // namespace blazeclaw::core
