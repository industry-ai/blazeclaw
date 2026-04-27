#include "core/PiEmbeddedService.h"
#include "gateway/GatewayToolRegistry.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using namespace blazeclaw::core;
using blazeclaw::gateway::ToolCatalogEntry;
using blazeclaw::gateway::ToolExecuteRequestV2;
using blazeclaw::gateway::ToolExecuteResultV2;

TEST_CASE("Embedded planner populates nano_pdf.generate content payload", "[embedded][nano-pdf][planner]") {
	PiEmbeddedService service;
	nlohmann::json capturedArgs = nlohmann::json::object();

	EmbeddedRuntimeExecutionRequest request{};
	request.run.sessionId = "main";
	request.run.message =
		"Please strictly execute in order and call nano-pdf to generate a professional report "
		"saved locally at '/tmp/Battery_Report.pdf' based on extracted market data.";
	request.runtimeTools = {
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
	};
	request.enforceOrderedAllowlist = true;
	request.orderedAllowedToolTargets = { "nano_pdf.generate" };
	request.toolExecutorV2 = [&capturedArgs](const ToolExecuteRequestV2& call) {
		if (call.argsJson.has_value()) {
			capturedArgs = nlohmann::json::parse(call.argsJson.value());
		}
		return ToolExecuteResultV2{
			.tool = call.tool,
			.executed = true,
			.status = "ok",
			.result = R"({"ok":true,"outputPath":"/tmp/Battery_Report_draft.pdf"})",
			.errorCode = "",
			.errorMessage = "",
			.startedAtMs = 1,
			.completedAtMs = 2,
			.latencyMs = 1,
			.correlationId = call.correlationId,
		};
	};

	const auto result = service.ExecuteRun(request);
	REQUIRE(result.handled);
	REQUIRE(result.success);
	REQUIRE(capturedArgs.contains("content"));
	REQUIRE(capturedArgs["content"].is_string());
	REQUIRE(capturedArgs["content"].get<std::string>().size() >= 32);
	REQUIRE(capturedArgs.contains("outputPath"));
	REQUIRE(capturedArgs["outputPath"] == "/tmp/Battery_Report_draft.pdf");
}

TEST_CASE("Embedded planner fails early when nano_pdf.generate content missing", "[embedded][nano-pdf][planner]") {
	PiEmbeddedService service;

	EmbeddedRuntimeExecutionRequest request{};
	request.run.sessionId = "main";
	request.run.message = "call nano-pdf";
	request.runtimeTools = {
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
	};
	request.enforceOrderedAllowlist = true;
	request.orderedAllowedToolTargets = { "nano_pdf.generate" };
	request.toolExecutorV2 = [](const ToolExecuteRequestV2& call) {
		(void)call;
		return ToolExecuteResultV2{
			.tool = "nano_pdf.generate",
			.executed = true,
			.status = "ok",
			.result = "{}",
			.errorCode = "",
			.errorMessage = "",
			.startedAtMs = 1,
			.completedAtMs = 2,
			.latencyMs = 1,
			.correlationId = "cid",
		};
	};

	const auto result = service.ExecuteRun(request);
	REQUIRE(result.handled);
	REQUIRE_FALSE(result.success);
	REQUIRE(result.reason == "planner_missing_pdf_content_payload");
	REQUIRE(result.errorCode == "embedded_invalid_args");
}
