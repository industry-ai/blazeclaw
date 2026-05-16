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

TEST_CASE("Cron add validator rejects delivery webhook without target", "[cron][schema]") {
	const RequestFrame request{
		.id = "3d",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"delivery\":{\"mode\":\"webhook\"}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.delivery.to") != std::string::npos);
}

TEST_CASE("Cron add validator rejects invalid failureDestination mode", "[cron][schema]") {
	const RequestFrame request{
		.id = "3e",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"delivery\":{\"mode\":\"announce\",\"failureDestination\":{\"mode\":\"none\"}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.delivery.failureDestination.mode") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects scope job without id", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-job-missing-id",
		.method = "cron.runs",
		.paramsJson = std::string("{\"scope\":\"job\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_missing_field");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects non-integer limit", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-non-integer-limit",
		.method = "cron.runs",
		.paramsJson = std::string("{\"limit\":1.5}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.limit") != std::string::npos);
}

TEST_CASE("Cron list validator rejects out-of-range limit", "[cron][schema]") {
	const RequestFrame request{
		.id = "list-bad-limit",
		.method = "cron.list",
		.paramsJson = std::string("{\"limit\":201}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.limit") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects empty statuses array", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-empty-statuses",
		.method = "cron.runs",
		.paramsJson = std::string("{\"statuses\":[]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.statuses") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects unsupported deliveryStatuses entry", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-bad-delivery-statuses",
		.method = "cron.runs",
		.paramsJson = std::string("{\"deliveryStatuses\":[\"unknown\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.deliveryStatuses") != std::string::npos);
}

TEST_CASE("Cron list validator accepts includeDisabled", "[cron][schema]") {
	const RequestFrame request{
		.id = "3b",
		.method = "cron.list",
		.paramsJson = std::string("{\"includeDisabled\":true,\"limit\":20}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron add validator accepts failureAlert false", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":false}")
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

TEST_CASE("Cron normalize patch clears nullable agent and session fields", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-1" },
		{ "name", "job" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hello" } } },
		{ "agentId", "agent-a" },
		{ "sessionKey", "session-a" },
		{ "state", CronJson::object() }
	};

	const CronJson patch = {
		{ "agentId", nullptr },
		{ "sessionKey", "" },
		{ "sessionTarget", "current" }
	};

	CronNormalize::ApplyPatch(job, patch);
	REQUIRE_FALSE(job.contains("agentId"));
	REQUIRE_FALSE(job.contains("sessionKey"));
	REQUIRE(job.value("sessionTarget", std::string()) == "isolated");
}

TEST_CASE("Cron normalize canonicalizes nested failureDestination fields", "[cron][normalize]") {
	const CronJson params = {
		{ "name", "delivery-shapes" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "ping" } } },
		{ "delivery",
			{
				{ "mode", "ANNOUNCE" },
				{ "bestEffort", "yes" },
				{ "failureDestination",
					{
						{ "mode", "INVALID" },
						{ "to", "  https://example.test/hook  " },
						{ "accountId", "  acc-1  " }
					} }
			} }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("delivery"));
	REQUIRE(normalized["delivery"].value("mode", std::string()) == "announce");
	REQUIRE_FALSE(normalized["delivery"].contains("bestEffort"));
	REQUIRE(normalized["delivery"].contains("failureDestination"));
	REQUIRE(normalized["delivery"]["failureDestination"].value("mode", std::string()) == "announce");
	REQUIRE(normalized["delivery"]["failureDestination"].value("to", std::string()) == "https://example.test/hook");
	REQUIRE(normalized["delivery"]["failureDestination"].value("accountId", std::string()) == "acc-1");
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

TEST_CASE("Cron store loads envelope with legacy values shape", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-envelope-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "{\"version\":0,\"kind\":\"jobs\",\"values\":[{\"id\":\"job-envelope\",\"name\":\"legacy\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]}";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "{\"version\":0,\"kind\":\"runs\",\"values\":[]}";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().is_array());
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-envelope");

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

TEST_CASE("Cron timer records non-retryable delivery target failure", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-webhook" },
			{ "name", "webhook job" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-url" } } },
			{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 5'000 }) } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed =
		timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE_FALSE(runs[0].value("retryScheduled", true));
	REQUIRE(runs[0].value("retryAttempt", 1) == 0);
	REQUIRE(jobs[0]["state"]["retryPendingUntilMs"].is_null());
}

TEST_CASE("Cron next run respects retry pending timestamp", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	const std::int64_t pendingMs = nowMs + 30'000;

	CronJson job = {
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "state", { { "retryPendingUntilMs", pendingMs } } }
	};

	const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
	REQUIRE(nextRun.has_value());
	REQUIRE(nextRun.value() == pendingMs);
}

TEST_CASE("Cron timer triggers failure alert after threshold", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert" },
			{ "name", "alert job" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "bad-target" } } },
			{ "failureAlert", { { "after", 2 }, { "cooldownMs", 10'000 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(runs[0].value("failureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs);
}

TEST_CASE("Cron timer respects failureAlert false disable", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-disabled" },
			{ "name", "alert disabled" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "bad-target" } } },
			{ "failureAlert", false },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 3 }, { "lastFailureAlertAtMs", nowMs - 1000 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
}

TEST_CASE("Wake validator rejects unsupported mode", "[cron][schema]") {
	const RequestFrame request{
		.id = "wake-bad-mode",
		.method = "wake",
		.paramsJson = std::string("{\"mode\":\"later\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.mode") != std::string::npos);
}

TEST_CASE("Cron update validator rejects empty id", "[cron][schema]") {
	const RequestFrame request{
		.id = "update-empty-id",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"\",\"patch\":{\"enabled\":false}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
}

TEST_CASE("Cron remove validator rejects empty jobId", "[cron][schema]") {
	const RequestFrame request{
		.id = "remove-empty-jobid",
		.method = "cron.remove",
		.paramsJson = std::string("{\"jobId\":\"\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.jobId") != std::string::npos);
}

TEST_CASE("Cron run validator rejects empty id", "[cron][schema]") {
	const RequestFrame request{
		.id = "run-empty-id",
		.method = "cron.run",
		.paramsJson = std::string("{\"id\":\"\",\"mode\":\"force\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
}

TEST_CASE("Cron runs validator accepts statuses and delivery filters", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-filters",
		.method = "cron.runs",
		.paramsJson = std::string(
			"{\"scope\":\"all\",\"statuses\":[\"ok\",\"error\"],\"deliveryStatuses\":[\"delivered\",\"not-delivered\"],\"deliveryStatus\":\"delivered\",\"limit\":20}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron timer run entry includes deliveryError for failed delivery", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-delivery-error" },
			{ "name", "delivery failure" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-url" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("deliveryError", std::string()) == "invalid webhook delivery target");
}

TEST_CASE("Cron timer records failureAlert suppression reason when disabled", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-disabled-meta" },
			{ "name", "alert disabled" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "bad-target" } } },
			{ "failureAlert", false },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 3 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "disabled");
}

TEST_CASE("Cron timer records failure destination metadata on delivery error", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination" },
			{ "name", "failure destination" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "failureDestination", { { "mode", "webhook" }, { "to", "invalid-destination" } } }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "invalid failure destination webhook target");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationStatus", std::string()) == "not-delivered");
}

TEST_CASE("Cron timer suppresses failure destination when same as primary target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-suppressed" },
			{ "name", "failure destination suppressed" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "failureDestination", { { "mode", "webhook" }, { "to", "invalid-url" } } }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "failure destination matches primary delivery target");
}

TEST_CASE("Cron timer suppresses failure alert for best-effort delivery", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-best-effort-alert" },
			{ "name", "best effort alert suppression" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-url" }, { "bestEffort", true } } },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "best_effort_delivery");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
}

TEST_CASE("Cron timer suppresses invalid failureAlert webhook target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-invalid-failure-alert-target" },
			{ "name", "invalid failureAlert target" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-url" } } },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "webhook" }, { "to", "bad-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "invalid_webhook_target");
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "webhook");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
}

TEST_CASE("Cron timer run ids are unique for same tick", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-unique-1" },
			{ "name", "unique 1" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "a" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		},
		{
			{ "id", "job-unique-2" },
			{ "name", "unique 2" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "b" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 2);
	REQUIRE(runs.size() == 2);
	REQUIRE(runs[0].contains("runId"));
	REQUIRE(runs[1].contains("runId"));
	REQUIRE(runs[0].value("runId", std::string()) != runs[1].value("runId", std::string()));
	REQUIRE(jobs[0]["state"].contains("lastRunId"));
	REQUIRE(jobs[1]["state"].contains("lastRunId"));
}

TEST_CASE("Cron run validator rejects unsupported mode", "[cron][schema]") {
	const RequestFrame request{
		.id = "run-bad-mode",
		.method = "cron.run",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"mode\":\"later\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.mode") != std::string::npos);
}
