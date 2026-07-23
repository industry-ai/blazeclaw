#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "chat/shared/ChatStateTelemetryConsolidation.h"

#include <catch2/catch_all.hpp>

namespace {
	using blazeclaw::chat::shared::RollbackSafetyEvaluator;
	using blazeclaw::chat::shared::SharedChatDiagnosticsCollector;
	using blazeclaw::chat::shared::StreamParityValidator;

	void RequireCommonSnapshotKeys(const nlohmann::json& snapshot)
	{
		REQUIRE(snapshot.is_object());
		REQUIRE(snapshot.contains("key"));
		REQUIRE(snapshot.contains("mode"));
		REQUIRE(snapshot.contains("chat.stream.delta.count"));
		REQUIRE(snapshot.contains("chat.stream.final.count"));
		REQUIRE(snapshot.contains("chat.stream.error.count"));
		REQUIRE(snapshot.contains("chat.stream.conformance.failures"));
		REQUIRE(snapshot.contains("chat.stream.parity.violations"));
		REQUIRE(snapshot.contains("chat.state.idempotency.duplicates"));
		REQUIRE(snapshot.contains("chat.state.active.count"));
		REQUIRE(snapshot.contains("chat.state.cancelled.count"));
		REQUIRE(snapshot.contains("chat.state.idempotency.keys"));
	}
}

TEST_CASE(
	"Shared stream parity validator enforces delta to terminal ordering",
	"[agentchat][corechat][parity][stream-order]")
{
	StreamParityValidator validator;

	REQUIRE(validator.Observe("corr-1", "delta"));
	REQUIRE(validator.Observe("corr-1", "delta"));
	REQUIRE(validator.Observe("corr-1", "final"));
	REQUIRE_FALSE(validator.Observe("corr-1", "delta"));
	REQUIRE_FALSE(validator.Observe("corr-1", "error"));

	REQUIRE(validator.Observe("corr-2", "error"));
	REQUIRE_FALSE(validator.Observe("corr-2", "final"));
}

TEST_CASE(
	"Shared diagnostics snapshot keeps aligned keys across chat modes",
	"[agentchat][corechat][parity][diagnostics]")
{
	SharedChatDiagnosticsCollector collector;
	collector.BeginRequest("req-1");
	collector.RecordStreamType("delta");
	collector.RecordStreamType("final");
	collector.RecordConformanceFailure();
	const auto firstObserved = collector.ObserveIdempotencyKey("req-1|delta|1");
	const auto duplicateObserved = collector.ObserveIdempotencyKey("req-1|delta|1");
	collector.CompleteRequest("req-1");
	REQUIRE(firstObserved);
	REQUIRE_FALSE(duplicateObserved);

	const auto snapshot = collector.Snapshot();
	const auto nativeJson =
		SharedChatDiagnosticsCollector::BuildSnapshotJson("agent-chat", snapshot);
	const auto legacyJson =
		SharedChatDiagnosticsCollector::BuildSnapshotJson("core-chat", snapshot);

	RequireCommonSnapshotKeys(nativeJson);
	RequireCommonSnapshotKeys(legacyJson);
	REQUIRE(nativeJson["key"] == legacyJson["key"]);
	REQUIRE(nativeJson["chat.stream.delta.count"] == legacyJson["chat.stream.delta.count"]);
	REQUIRE(nativeJson["chat.stream.final.count"] == legacyJson["chat.stream.final.count"]);
	REQUIRE(nativeJson["chat.state.idempotency.duplicates"] == legacyJson["chat.state.idempotency.duplicates"]);
}

TEST_CASE(
	"Rollback safety evaluator validates native legacy and auto transitions",
	"[agentchat][parity][rollback]")
{
	REQUIRE(RollbackSafetyEvaluator::IsSafeModeTransition("native", true, false));
	REQUIRE_FALSE(RollbackSafetyEvaluator::IsSafeModeTransition("native", false, false));

	REQUIRE(RollbackSafetyEvaluator::IsSafeModeTransition("legacy", false, true));
	REQUIRE_FALSE(RollbackSafetyEvaluator::IsSafeModeTransition("legacy", false, false));

	REQUIRE(RollbackSafetyEvaluator::IsSafeModeTransition("auto", true, false));
	REQUIRE(RollbackSafetyEvaluator::IsSafeModeTransition("auto", false, false));
	REQUIRE_FALSE(RollbackSafetyEvaluator::IsSafeModeTransition("auto", false, true));
}
