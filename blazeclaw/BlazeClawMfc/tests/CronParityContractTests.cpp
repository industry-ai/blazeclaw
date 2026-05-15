#include "pch.h"

#include "../src/cron/CronNormalize.h"
#include "../src/cron/CronStoreService.h"
#include "../src/cron/CronTimerService.h"
#include "../src/gateway/GatewayProtocolSchemaValidator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {
	using blazeclaw::cron::CronJson;
	using blazeclaw::cron::CronNormalize;
	using blazeclaw::cron::CronStoreService;
	using blazeclaw::cron::CronTimerService;
	using blazeclaw::gateway::protocol::GatewayProtocolSchemaValidator;
	using blazeclaw::gateway::protocol::RequestFrame;
	using blazeclaw::gateway::protocol::SchemaValidationIssue;
}

TEST_CASE("Cron normalize defaults wake/session target", "[cron][normalize]") {
	const CronJson params = {
		{ "name", "nightly" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "event", "sync" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.value("wakeMode", std::string()) == "now");
	REQUIRE(normalized.value("sessionTarget", std::string()) == "main");
	REQUIRE(normalized.value("enabled", false));
}

TEST_CASE("Cron add validator enforces required fields", "[cron][schema]") {
	const RequestFrame request{
		.id = "1",
		.method = "cron.add",
		.paramsJson = std::string("{\"name\":\"job\",\"payload\":{}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_missing_field");
	REQUIRE(issue.message.find("params.schedule") != std::string::npos);
}

TEST_CASE("Wake validator rejects unknown field", "[cron][schema]") {
	const RequestFrame request{
		.id = "2",
		.method = "wake",
		.paramsJson = std::string("{\"mode\":\"now\",\"unknown\":true}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.unknown") != std::string::npos);
}

TEST_CASE("Cron update validator accepts id and patch", "[cron][schema]") {
	const RequestFrame request{
		.id = "3",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"enabled\":false}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron normalize coerces legacy schedule/payload shapes", "[cron][normalize]") {
	const CronJson params = {
		{ "name", "legacy-job" },
		{ "schedule", { { "cron", "0 9 * * *" } } },
		{ "payload", { { "text", "daily report" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized["schedule"].value("kind", std::string()) == "cron");
	REQUIRE(normalized["schedule"].value("expr", std::string()) == "0 9 * * *");
	REQUIRE(normalized["payload"].value("kind", std::string()) == "systemEvent");
	REQUIRE(normalized["payload"].value("text", std::string()) == "daily report");
}

TEST_CASE("Cron store loads legacy array shape and rewrites envelope", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[{\"id\":\"job-1\",\"name\":\"legacy\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().is_array());
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-1");

	store.SaveJobs();

	std::ifstream saved(jobsPath, std::ios::binary);
	REQUIRE(saved.is_open());
	CronJson savedJson;
	saved >> savedJson;
	REQUIRE(savedJson.is_object());
	REQUIRE(savedJson.value("version", 0) == 1);
	REQUIRE(savedJson.value("kind", std::string()) == "jobs");
	REQUIRE(savedJson.contains("values"));
	REQUIRE(savedJson["values"].is_array());

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron timer computes daily cron expression minute/hour", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000; // stable fixture timestamp
	const std::int64_t minuteMs = 60'000;
	const std::int64_t hourMs = 60 * minuteMs;
	const std::int64_t dayMs = 24 * hourMs;

	CronJson job = {
		{ "enabled", true },
		{ "schedule", { { "kind", "cron" }, { "expr", "0 9 * * *" } } },
		{ "state", CronJson::object() }
	};

	const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
	REQUIRE(nextRun.has_value());

	const std::int64_t dayStart = (nowMs / dayMs) * dayMs;
	std::int64_t expected = dayStart + 9 * hourMs;
	if (expected <= nowMs) {
		expected += dayMs;
	}
	REQUIRE(nextRun.value() == expected);
}
