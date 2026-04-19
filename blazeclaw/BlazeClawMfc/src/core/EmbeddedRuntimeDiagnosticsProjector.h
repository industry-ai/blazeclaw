#pragma once

#include "diagnostics/DiagnosticsSnapshot.h"

#include <cstdint>
#include <string>

namespace blazeclaw::core {

    class EmbeddedRuntimeDiagnosticsProjector {
    public:
        struct Context {
            std::size_t activeRuns = 0;
            bool dynamicLoopEnabled = false;
            bool canaryEligible = false;
            bool promotionReady = false;
            std::uint64_t promotionMinRuns = 0;
            double promotionMinSuccessRate = 0.0;
            bool fallbackUsed = false;
            std::string fallbackReason;
            std::uint64_t runSuccess = 0;
            std::uint64_t runFailure = 0;
            std::uint64_t runTimeout = 0;
            std::uint64_t runCancelled = 0;
            std::uint64_t runFallback = 0;
            std::uint64_t taskDeltaTransitions = 0;
        };

        void Apply(
            const Context& context,
            DiagnosticsSnapshot& snapshot) const {
            snapshot.embeddedActiveRuns = context.activeRuns;
            snapshot.embeddedDynamicLoopEnabled = context.dynamicLoopEnabled;
            snapshot.embeddedCanaryEligible = context.canaryEligible;
            snapshot.embeddedPromotionReady = context.promotionReady;
            snapshot.embeddedPromotionMinRuns = context.promotionMinRuns;
            snapshot.embeddedPromotionMinSuccessRate =
                context.promotionMinSuccessRate;
            snapshot.embeddedFallbackUsed = context.fallbackUsed;
            snapshot.embeddedFallbackReason = context.fallbackReason;
            snapshot.embeddedTotalRuns = context.runSuccess + context.runFailure;
            snapshot.embeddedSuccessRate = snapshot.embeddedTotalRuns == 0
                ? 0.0
                : static_cast<double>(context.runSuccess) /
                static_cast<double>(snapshot.embeddedTotalRuns);
            snapshot.embeddedRunSuccess = context.runSuccess;
            snapshot.embeddedRunFailure = context.runFailure;
            snapshot.embeddedRunTimeout = context.runTimeout;
            snapshot.embeddedRunCancelled = context.runCancelled;
            snapshot.embeddedRunFallback = context.runFallback;
            snapshot.embeddedTaskDeltaTransitions =
                context.taskDeltaTransitions;
        }
    };

} // namespace blazeclaw::core
