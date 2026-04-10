#include "pch.h"
#include "RecoveryPolicyEngine.h"

#include <algorithm>
#include <chrono>

namespace blazeclaw::gateway {
	namespace {
		std::uint64_t CurrentEpochMsRecovery() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		void AppendRecoveryDelta(
			RecoveryOutcome& outcome,
			const RecoveryRequest& request,
			const std::string& phase,
			const std::string& status,
			const std::string& stepLabel,
			const std::string& errorCode = {}) {
			const std::uint64_t nowMs = CurrentEpochMsRecovery();
			outcome.recoveryDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
				.index = outcome.recoveryDeltas.size(),
				.runId = request.runId,
				.sessionId = request.sessionKey,
				.phase = phase,
				.status = status,
				.errorCode = errorCode,
				.startedAtMs = nowMs,
				.completedAtMs = nowMs,
				.latencyMs = 0,
				.stepLabel = stepLabel,
				});
		}

		std::string BuildTerminalErrorCode(const FailureClassification& classification) {
			if (!classification.canonicalCode.empty()) {
				return classification.canonicalCode;
			}

			return "recovery_terminal";
		}
	}

	RecoveryOutcome RecoveryPolicyEngine::Execute(
		const RecoveryRequest& request,
		RunLoopBudget& budget) {
		RecoveryOutcome outcome;
		outcome.normalizedDeltas = request.taskDeltas;

		if (!budget.ConsumeIteration()) {
			outcome.terminalErrorCode = "recovery_budget_exhausted";
			outcome.terminalErrorMessage = "Recovery budget exhausted before evaluation.";
			AppendRecoveryDelta(
				outcome,
				request,
				"fallback",
				"failed",
				"recovery_budget",
				outcome.terminalErrorCode);
			return outcome;
		}

		const FailureClassification classification = FailureClassifier::Classify(
			request.errorCode,
			request.errorMessage);

		AppendRecoveryDelta(
			outcome,
			request,
			"fallback",
			"classified",
			"failure_classification",
			classification.canonicalCode);

		if (classification.retryable && budget.ConsumeRetry()) {
			outcome.shouldRetry = true;
			outcome.shouldReinvokeRuntime = true;
			AppendRecoveryDelta(
				outcome,
				request,
				"fallback",
				"retry",
				"retry_branch",
				classification.canonicalCode);
			return outcome;
		}

		if (classification.category == FailureCategory::Auth &&
			budget.ConsumeProfileFallback()) {
			const AuthProfileDecision profileDecision =
				AuthProfilePolicyService::SelectFallbackProfile(
					request.authProfileId,
					classification.canonicalCode);
			outcome.selectedProfileId = profileDecision.selectedProfileId;
			if (profileDecision.fallbackApplied) {
				outcome.recovered = true;
				outcome.shouldReinvokeRuntime = true;
				AppendRecoveryDelta(
					outcome,
					request,
					"fallback",
					"profile_fallback",
					"profile_fallback_branch",
					profileDecision.reasonCode);
				return outcome;
			}
		}

		if (budget.ConsumeCompaction()) {
			const CompactionResult compaction = CompactionCoordinator::TryCompact(
				request.message,
				classification.canonicalCode);
			if (compaction.applied) {
				outcome.compactionApplied = true;
				outcome.recovered = true;
				outcome.shouldReinvokeRuntime = true;
				const ContextEngineSelection contextSelection =
					ContextEnginePolicySelector::Select(
						request.message,
						classification.canonicalCode);
				outcome.selectedContextEngineId = contextSelection.engineId;
				AppendRecoveryDelta(
					outcome,
					request,
					"fallback",
					"compaction",
					"compaction_branch",
					compaction.reasonCode);
				return outcome;
			}
		}

		if (budget.ConsumeTruncation()) {
			const ToolResultTruncationResult truncation =
				ToolResultTruncationCoordinator::TryTruncate(request.taskDeltas);
			if (truncation.applied) {
				outcome.truncationApplied = true;
				outcome.recovered = true;
				outcome.shouldReinvokeRuntime = true;
				outcome.normalizedDeltas = truncation.deltas;
				AppendRecoveryDelta(
					outcome,
					request,
					"fallback",
					"truncation",
					"tool_result_truncation_branch",
					"tool_result_truncation");
				return outcome;
			}
		}

		outcome.terminalErrorCode = BuildTerminalErrorCode(classification);
		outcome.terminalErrorMessage = request.errorMessage.empty()
			? "Recovery chain exhausted; terminal failure."
			: request.errorMessage;
		AppendRecoveryDelta(
			outcome,
			request,
			"final",
			"failed",
			"recovery_terminal",
			outcome.terminalErrorCode);
		return outcome;
	}

} // namespace blazeclaw::gateway
