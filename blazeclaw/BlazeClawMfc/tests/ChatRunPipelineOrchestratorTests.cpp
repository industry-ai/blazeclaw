#include "gateway/ChatRunPipelineOrchestrator.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("ChatRunPipelineOrchestrator executes Workstream A stage order", "[pipeline][workstream-a]") {
	ChatRunPipelineOrchestrator orchestrator;
	ChatRunStageContext context;
	context.requestId = "req-1";
	context.method = "chat.send";
	context.paramsJson =
		"{\"sessionKey\":\"main\",\"message\":\"today weather summary\",\"idempotencyKey\":\"idem-1\"}";

	const auto result = orchestrator.Run(context);

	REQUIRE(result.ok);
	REQUIRE(result.status == "completed");
	REQUIRE(context.sessionKey == "main");
	REQUIRE(context.idempotencyKey == "idem-1");
	REQUIRE_FALSE(context.runtimeMessage.empty());
	REQUIRE_FALSE(context.preferChineseResponse);
	REQUIRE_FALSE(context.runId.empty());
	REQUIRE(context.stageTrace == std::vector<std::string>{
		"transport",
			"control",
			"decomposition",
			"runtime",
			"recovery",
			"finalize",
	});
}

TEST_CASE("ChatRunPipelineOrchestrator control stage short-circuits on idempotency dedupe", "[pipeline][workstream-a]") {
	ChatRunPipelineOrchestrator orchestrator;
	ChatRunStageContext context;
	context.requestId = "req-dedupe";
	context.method = "chat.send";
	context.paramsJson =
		"{\"sessionKey\":\"main\",\"message\":\"hello\",\"idempotencyKey\":\"idem-dup\"}";
	context.validateAttachments = [](
		const std::optional<std::string>&,
		bool& hasAttachments,
		std::string&,
		std::string&) {
			hasAttachments = false;
			return true;
		};
	context.findRunByIdempotency = [](const std::string& key) -> std::optional<std::string> {
		if (key == "idem-dup") {
			return std::string("run-existing");
		}

		return std::nullopt;
		};

	const auto result = orchestrator.Run(context);

	REQUIRE(result.ok);
	REQUIRE(result.status == "deduped");
	REQUIRE(context.shouldReturnEarly);
	REQUIRE(context.deduped);
	REQUIRE(context.dedupedRunId == "run-existing");
	REQUIRE(context.stageTrace == std::vector<std::string>{
		"transport",
			"control",
	});
}

TEST_CASE("ChatRunPipelineOrchestrator control stage short-circuits on attachment validation", "[pipeline][workstream-a]") {
	ChatRunPipelineOrchestrator orchestrator;
	ChatRunStageContext context;
	context.requestId = "req-invalid-attachment";
	context.method = "chat.send";
	context.paramsJson =
		"{\"sessionKey\":\"main\",\"message\":\"hello\",\"attachments\":{}}";
	context.validateAttachments = [](
		const std::optional<std::string>&,
		bool& hasAttachments,
		std::string& errorCode,
		std::string& errorMessage) {
			hasAttachments = false;
			errorCode = "invalid_attachments";
			errorMessage = "attachments must be array";
			return false;
		};

	const auto result = orchestrator.Run(context);

	REQUIRE(result.ok);
	REQUIRE(result.status == "validation_failed");
	REQUIRE(context.shouldReturnEarly);
	REQUIRE_FALSE(context.responseOk);
	REQUIRE(context.responseErrorCode == "invalid_attachments");
	REQUIRE(context.responseErrorMessage == "attachments must be array");
	REQUIRE(context.stageTrace == std::vector<std::string>{
		"transport",
			"control",
	});
}
