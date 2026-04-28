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

TEST_CASE("Embedded planner auto-splits generate intent into nano_pdf.generate then nano_pdf.edit", "[embedded][nano-pdf][planner]") {
	PiEmbeddedService service;
	std::vector<std::string> executedTools;

	EmbeddedRuntimeExecutionRequest request{};
	request.run.sessionId = "main";
	request.run.message =
		"Please generate a well-structured business brief and call nano-pdf to output a professional PDF at '/tmp/Battery_Report.pdf'.";
	request.runtimeTools = {
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.edit", .label = "Nano PDF Edit", .category = "document", .enabled = true },
	};
	request.enforceOrderedAllowlist = true;
	request.orderedAllowedToolTargets = { "nano_pdf.generate" };
	request.toolExecutorV2 = [&executedTools](const ToolExecuteRequestV2& call) {
		executedTools.push_back(call.tool);
		const bool isGenerate = call.tool == "nano_pdf.generate";
		return ToolExecuteResultV2{
			.tool = call.tool,
			.executed = true,
			.status = "ok",
			.result = isGenerate ? R"({"ok":true,"outputPath":"/tmp/Battery_Report_draft.pdf"})" : R"({"ok":true,"outputPath":"/tmp/Battery_Report.pdf"})",
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
	REQUIRE(executedTools.size() >= 2);
	REQUIRE(executedTools[0] == "nano_pdf.generate");
	REQUIRE(executedTools[1] == "nano_pdf.edit");
}

TEST_CASE("Embedded planner preserves explicit nano_pdf.edit with inputPath", "[embedded][nano-pdf][planner]") {
	PiEmbeddedService service;
	std::vector<std::string> executedTools;

	EmbeddedRuntimeExecutionRequest request{};
	request.run.sessionId = "main";
	request.run.message =
		"Please call nano_pdf.edit with inputPath '/tmp/Battery_Report_draft.pdf' and output '/tmp/Battery_Report.pdf'.";
	request.runtimeTools = {
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.edit", .label = "Nano PDF Edit", .category = "document", .enabled = true },
	};
	request.enforceOrderedAllowlist = true;
	request.orderedAllowedToolTargets = { "nano_pdf.edit" };
	request.toolExecutorV2 = [&executedTools](const ToolExecuteRequestV2& call) {
		executedTools.push_back(call.tool);
		return ToolExecuteResultV2{
			.tool = call.tool,
			.executed = true,
			.status = "ok",
			.result = R"({"ok":true,"outputPath":"/tmp/Battery_Report.pdf"})",
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
	REQUIRE(executedTools.size() == 1);
	REQUIRE(executedTools[0] == "nano_pdf.edit");
}

TEST_CASE("Embedded planner executes original three-step prompt with baidu/web-browsing then nano-pdf split", "[embedded][nano-pdf][planner][e2e]") {
	PiEmbeddedService service;
	std::vector<std::string> executedTools;

	EmbeddedRuntimeExecutionRequest request{};
	request.run.sessionId = "main";
	request.run.message =
		"Please strictly execute in order: 1. Call `baidu-search` to search for \"2024 commercialization progress of solid-state batteries\"; "
		"2. Call `web-browsing` to deeply read the main text of the top two search results and extract core data; "
		"3. Based on the extracted data, write a well-structured business brief, and call `nano-pdf` to format it into a professional PDF file saved locally at '/tmp/Battery_Report.pdf'";
	request.runtimeTools = {
		ToolCatalogEntry{.id = "baidu-search.search.web", .label = "Baidu Search", .category = "web", .enabled = true },
		ToolCatalogEntry{.id = "web_browsing.fetch.content", .label = "Web Browsing", .category = "web", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.edit", .label = "Nano PDF Edit", .category = "document", .enabled = true },
	};
	request.enforceOrderedAllowlist = true;
	request.orderedAllowedToolTargets = {
		"baidu-search.search.web",
		"web_browsing.fetch.content",
		"nano_pdf.generate",
	};
	request.toolExecutorV2 = [&executedTools](const ToolExecuteRequestV2& call) {
		executedTools.push_back(call.tool);
		if (call.tool == "baidu-search.search.web") {
			return ToolExecuteResultV2{
				.tool = call.tool,
				.executed = true,
				.status = "ok",
				.result = R"({"ok":true,"results":[{"title":"Result1"},{"title":"Result2"}]})",
				.errorCode = "",
				.errorMessage = "",
				.startedAtMs = 1,
				.completedAtMs = 2,
				.latencyMs = 1,
				.correlationId = call.correlationId,
			};
		}
		if (call.tool == "web_browsing.fetch.content") {
			return ToolExecuteResultV2{
				.tool = call.tool,
				.executed = true,
				.status = "ok",
				.result = "Solid-state battery commercialization progressed through pilot production, automotive qualification, and gigawatt-scale planning in 2024, with clear milestones in yield, safety validation, and cost-down roadmaps.",
				.errorCode = "",
				.errorMessage = "",
				.startedAtMs = 3,
				.completedAtMs = 4,
				.latencyMs = 1,
				.correlationId = call.correlationId,
			};
		}
		if (call.tool == "nano_pdf.generate") {
			return ToolExecuteResultV2{
				.tool = call.tool,
				.executed = true,
				.status = "ok",
				.result = R"({"ok":true,"outputPath":"/tmp/Battery_Report_draft.pdf"})",
				.errorCode = "",
				.errorMessage = "",
				.startedAtMs = 5,
				.completedAtMs = 6,
				.latencyMs = 1,
				.correlationId = call.correlationId,
			};
		}
		return ToolExecuteResultV2{
			.tool = call.tool,
			.executed = true,
			.status = "ok",
			.result = R"({"ok":true,"outputPath":"/tmp/Battery_Report.pdf"})",
			.errorCode = "",
			.errorMessage = "",
			.startedAtMs = 7,
			.completedAtMs = 8,
			.latencyMs = 1,
			.correlationId = call.correlationId,
		};
	};

	const auto result = service.ExecuteRun(request);
	REQUIRE(result.handled);
	REQUIRE(result.success);
	REQUIRE(executedTools.size() >= 4);
	REQUIRE(executedTools[0] == "baidu-search.search.web");
	REQUIRE(executedTools[1] == "web_browsing.fetch.content");
	REQUIRE(executedTools[2] == "nano_pdf.generate");
	REQUIRE(executedTools[3] == "nano_pdf.edit");
}
