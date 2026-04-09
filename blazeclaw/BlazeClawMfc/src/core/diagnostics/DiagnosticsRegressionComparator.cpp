#include "pch.h"
#include "DiagnosticsRegressionComparator.h"

namespace blazeclaw::core {

	namespace {

		template<typename TValue>
		void CompareField(
			const std::string& name,
			const TValue& before,
			const TValue& after,
			DiagnosticsRegressionComparator::ComparisonResult& result)
		{
			if (before == after)
			{
				return;
			}

			result.equivalent = false;
			result.differences.push_back(
				DiagnosticsRegressionComparator::Difference{
					.field = name,
					.beforeValue = std::to_string(before),
					.afterValue = std::to_string(after),
				});
		}

		void CompareStringField(
			const std::string& name,
			const std::string& before,
			const std::string& after,
			DiagnosticsRegressionComparator::ComparisonResult& result)
		{
			if (before == after)
			{
				return;
			}

			result.equivalent = false;
			result.differences.push_back(
				DiagnosticsRegressionComparator::Difference{
					.field = name,
					.beforeValue = before,
					.afterValue = after,
				});
		}

		void CompareBoolField(
			const std::string& name,
			const bool before,
			const bool after,
			DiagnosticsRegressionComparator::ComparisonResult& result)
		{
			if (before == after)
			{
				return;
			}

			result.equivalent = false;
			result.differences.push_back(
				DiagnosticsRegressionComparator::Difference{
					.field = name,
					.beforeValue = before ? "true" : "false",
					.afterValue = after ? "true" : "false",
				});
		}

	} // namespace

	DiagnosticsRegressionComparator::ComparisonResult
		DiagnosticsRegressionComparator::CompareSelectedFields(
			const DiagnosticsSnapshot& before,
			const DiagnosticsSnapshot& after) const
	{
		ComparisonResult result;

		CompareBoolField(
			"runtime.running",
			before.runtimeRunning,
			after.runtimeRunning,
			result);
		CompareStringField(
			"runtime.gatewayWarning",
			before.gatewayWarning,
			after.gatewayWarning,
			result);

		CompareField(
			"emailFallback.fallbackAttempts",
			before.emailFallbackAttempts,
			after.emailFallbackAttempts,
			result);
		CompareField(
			"emailFallback.fallbackSuccess",
			before.emailFallbackSuccess,
			after.emailFallbackSuccess,
			result);
		CompareField(
			"emailFallback.fallbackFailure",
			before.emailFallbackFailure,
			after.emailFallbackFailure,
			result);
		CompareStringField(
			"emailFallback.capabilityState",
			before.emailCapabilityState,
			after.emailCapabilityState,
			result);

		CompareField(
			"skills.catalogEntries",
			before.skillsCatalogEntries,
			after.skillsCatalogEntries,
			result);
		CompareField(
			"skills.promptIncluded",
			before.skillsPromptIncluded,
			after.skillsPromptIncluded,
			result);

		CompareField(
			"hooks.dispatches",
			before.hooksDispatches,
			after.hooksDispatches,
			result);
		CompareField(
			"hooks.policyBlocked",
			before.hooksPolicyBlocked,
			after.hooksPolicyBlocked,
			result);
		CompareField(
			"hooks.driftDetected",
			before.hooksDriftDetected,
			after.hooksDriftDetected,
			result);
		CompareStringField(
			"hooks.lastDriftReason",
			before.hooksLastDriftReason,
			after.hooksLastDriftReason,
			result);

		CompareField(
			"embedded.totalRuns",
			before.embeddedTotalRuns,
			after.embeddedTotalRuns,
			result);
		CompareField(
			"embedded.taskDeltaTransitions",
			before.embeddedTaskDeltaTransitions,
			after.embeddedTaskDeltaTransitions,
			result);

		if (result.equivalent)
		{
			result.summary = "diagnostics parity stable";
		}
		else
		{
			result.summary =
				"diagnostics parity drift detected: " +
				std::to_string(result.differences.size()) +
				" field(s) changed";
		}

		return result;
	}

} // namespace blazeclaw::core
