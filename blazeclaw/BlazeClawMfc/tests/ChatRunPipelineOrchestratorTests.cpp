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
