#include "gateway/ChatRunPipelineOrchestrator.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("ChatRunPipelineOrchestrator executes Workstream A stage order", "[pipeline][workstream-a]") {
	ChatRunPipelineOrchestrator orchestrator;
	ChatRunStageContext context;
	context.requestId = "req-1";
	context.method = "chat.send";

	const auto result = orchestrator.Run(context);

	REQUIRE(result.ok);
	REQUIRE(result.status == "completed");
	REQUIRE(context.stageTrace == std::vector<std::string>{
		"transport",
			"control",
			"decomposition",
			"runtime",
			"recovery",
			"finalize",
	});
}
