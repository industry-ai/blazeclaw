#include "gateway/TaskDeltaLegacyAdapter.h"
#include "gateway/TaskDeltaSchemaValidator.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("TaskDelta contract: legacy adapter materializes required fields", "[taskdelta][contract]") {
	GatewayHost::ChatRuntimeResult::TaskDeltaEntry legacy;
	legacy.phase = "tool_result";
	legacy.toolName = "weather.lookup";

	const auto adapted = TaskDeltaLegacyAdapter::AdaptEntry(
		legacy,
		"run-1",
		"main",
		0);

	REQUIRE(adapted.runId == "run-1");
	REQUIRE(adapted.schemaVersion == 1);
	REQUIRE(adapted.sessionId == "main");
	REQUIRE(adapted.status == "ok");
	REQUIRE(adapted.stepLabel == "tool_result");
}

TEST_CASE("TaskDelta contract: schema validator rejects unsupported schema version", "[taskdelta][contract]") {
	GatewayHost::ChatRuntimeResult::TaskDeltaEntry entry;
	entry.schemaVersion = 2;
	entry.index = 0;
	entry.runId = "run-version";
	entry.sessionId = "main";
	entry.phase = "plan";
	entry.status = "ok";
	entry.stepLabel = "execution_plan";

	std::string code;
	std::string message;
	REQUIRE_FALSE(TaskDeltaSchemaValidator::ValidateEntry(entry, code, message));
	REQUIRE(code == "unsupported_schema_version");
}

TEST_CASE("TaskDelta contract: schema validator enforces deterministic indexes", "[taskdelta][contract]") {
	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> entries;
	entries.push_back(TaskDeltaLegacyAdapter::AdaptEntry(
		GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = 0,
			.runId = "run-x",
			.sessionId = "main",
			.phase = "plan",
			.status = "ok",
			.stepLabel = "execution_plan",
		},
		"run-x",
		"main",
		0));
	entries.push_back(TaskDeltaLegacyAdapter::AdaptEntry(
		GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = 3,
			.runId = "run-x",
			.sessionId = "main",
			.phase = "final",
			.status = "completed",
			.stepLabel = "run_terminal",
		},
		"run-x",
		"main",
		1));

	std::string code;
	std::string message;
	REQUIRE(TaskDeltaSchemaValidator::ValidateRun("run-x", entries, code, message));
}

TEST_CASE("TaskDelta contract: schema validator rejects run mismatch", "[taskdelta][contract]") {
	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> entries;
	entries.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
		.index = 0,
		.runId = "wrong-run",
		.sessionId = "main",
		.phase = "plan",
		.status = "ok",
		.stepLabel = "execution_plan",
		});

	std::string code;
	std::string message;
	REQUIRE_FALSE(TaskDeltaSchemaValidator::ValidateRun("run-y", entries, code, message));
	REQUIRE(code == "run_id_mismatch");
}
