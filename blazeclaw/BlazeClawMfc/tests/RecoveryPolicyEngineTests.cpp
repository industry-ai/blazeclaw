#include "gateway/RecoveryPolicyEngine.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("Recovery policy classifies timeout as retryable", "[recovery][classifier]") {
	const auto classification = FailureClassifier::Classify(
		"embedded_deadline_exceeded",
		"deadline exceeded during runtime loop");

	REQUIRE(classification.category == FailureCategory::Timeout);
	REQUIRE(classification.retryable);
	REQUIRE(classification.canonicalCode == "timeout");
}

TEST_CASE("Recovery policy applies retry when budget allows", "[recovery][budget]") {
	RunLoopBudget budget;
	const auto outcome = RecoveryPolicyEngine::Execute(
		RecoveryRequest{
			.runId = "run-recovery-1",
			.sessionKey = "main",
			.message = "Long task",
			.errorCode = "overloaded",
			.errorMessage = "provider overloaded",
			.authProfileId = "default",
			.taskDeltas = {},
		},
		budget);

	REQUIRE(outcome.shouldRetry);
	REQUIRE(outcome.shouldReinvokeRuntime);
	REQUIRE_FALSE(outcome.terminalErrorCode.size() > 0);
}

TEST_CASE("Recovery policy ends terminally when branch budgets are exhausted", "[recovery][terminal]") {
	RunLoopBudget budget(RunLoopBudget::Limits{
		.maxIterations = 1,
		.maxRetry = 0,
		.maxProfileFallback = 0,
		.maxCompaction = 0,
		.maxTruncation = 0,
		});

	const auto outcome = RecoveryPolicyEngine::Execute(
		RecoveryRequest{
			.runId = "run-recovery-2",
			.sessionKey = "main",
			.message = "Task",
			.errorCode = "auth_failed",
			.errorMessage = "auth failed",
			.authProfileId = "default",
			.taskDeltas = {},
		},
		budget);

	REQUIRE_FALSE(outcome.recovered);
	REQUIRE_FALSE(outcome.shouldReinvokeRuntime);
	REQUIRE(outcome.terminalErrorCode.size() > 0);
}
