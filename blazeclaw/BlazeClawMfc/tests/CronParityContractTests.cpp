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
	using blazeclaw::gateway::protocol::ResponseFrame;
	using blazeclaw::gateway::protocol::SchemaValidationIssue;
}

TEST_CASE("Cron runs validator rejects statuses array above max cardinality", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-statuses-too-many",
		.method = "cron.runs",
		.paramsJson = std::string("{\"statuses\":[\"ok\",\"error\",\"skipped\",\"queued\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.statuses") != std::string::npos);
	REQUIRE(issue.message.find("at most 3") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects deliveryStatuses array above max cardinality", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-delivery-statuses-too-many",
		.method = "cron.runs",
		.paramsJson = std::string("{\"deliveryStatuses\":[\"not-requested\",\"delivered\",\"not-delivered\",\"suppressed\",\"delivered\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.deliveryStatuses") != std::string::npos);
	REQUIRE(issue.message.find("at most 4") != std::string::npos);
}

TEST_CASE("Cron status response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-status-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"enabled\":true,\"storePath\":\"state/cron.jobs.json\",\"jobs\":2,\"nextWakeAtMs\":null}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.status", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-status-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"enabled\":true,\"storePath\":\"state/cron.jobs.json\",\"jobs\":2,\"nextWakeAtMs\":\"soon\"}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.status", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron list response validator enforces pagination shape", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-list-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"jobs\":[],\"total\":0,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.list", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-list-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"jobs\":[],\"total\":0,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":\"false\"}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.list", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron runs response validator enforces entries contract", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-runs-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[],\"total\":0,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-runs-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":{},\"total\":0,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron timer suppresses announce failure destination when target equals primary announce target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-target-match" },
			{ "name", "failure destination announce target match" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "target-1" },
					{ "failureDestination",
						{
							{ "mode", "announce" },
							{ "to", "target-1" },
							{ "channel", "last" },
							{ "accountId", "" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "target-1");
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "failure destination matches primary delivery target");
}

TEST_CASE("Cron timer does not suppress announce failure destination when primary mode is none", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-primary-none" },
			{ "name", "failure destination announce primary none" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "none" },
					{ "failureDestination",
						{
							{ "mode", "announce" },
							{ "to", "target-1" },
							{ "channel", "last" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-requested");
}

TEST_CASE("Cron timer suppresses failureAlert when not explicitly configured", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-not-configured" },
			{ "name", "alert not configured" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "bad-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 3 }, { "lastFailureAlertAtMs", nowMs - 1'000 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "not_configured");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
	REQUIRE(jobs[0]["state"].value("failureAlertSuppressedReason", std::string()) == "not_configured");
}

TEST_CASE("Cron timer clears lastFailureAlertAtMs on successful run", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-clear-success" },
			{ "name", "alert clear success" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "announce" }, { "to", "team" } } },
			{ "failureAlert", { { "after", 2 }, { "cooldownMs", 10'000 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 2 }, { "lastFailureAlertAtMs", nowMs - 1000 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(jobs[0]["state"].value("consecutiveErrors", 1) == 0);
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
}

TEST_CASE("Cron timer clears lastFailureAlertAtMs on skipped run", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-clear-skipped" },
			{ "name", "alert clear skipped" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "" } } },
			{ "failureAlert", { { "after", 2 }, { "cooldownMs", 10'000 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 2 }, { "lastFailureAlertAtMs", nowMs - 1000 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "skipped");
	REQUIRE(jobs[0]["state"].value("consecutiveErrors", 1) == 0);
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
}

TEST_CASE("Cron timer announce failure destination falls back to primary target when to is omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-fallback-primary-to" },
			{ "name", "failure destination announce fallback primary to" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "failureDestination",
						{
							{ "mode", "announce" },
							{ "channel", "alerts" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "invalid-url");
	REQUIRE(runs[0].value("failureDestinationChannel", std::string()) == "alerts");
	REQUIRE(runs[0].value("failureDestinationAttempted", false));
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationStatus", std::string()) == "delivered");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationTarget", std::string()) == "invalid-url");
}

TEST_CASE("Cron runs validator accepts manual lifecycle statuses array values within max cardinality", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-manual-lifecycle-statuses",
		.method = "cron.runs",
		.paramsJson = std::string(
			"{\"scope\":\"all\",\"statuses\":[\"queued\",\"running\",\"failed\"],\"limit\":20}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron runs validator rejects unsupported statuses entry", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-bad-statuses-entry",
		.method = "cron.runs",
		.paramsJson = std::string("{\"statuses\":[\"paused\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.statuses") != std::string::npos);
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

TEST_CASE("Cron normalize resolves sessionTarget current using sessionKey context", "[cron][normalize]") {
	const CronJson params = {
		{ "name", "nightly" },
		{ "sessionTarget", "current" },
		{ "sessionKey", "agent:main:work" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "run" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.value("sessionTarget", std::string()) == "session:agent:main:work");
}

TEST_CASE("Cron patch resolves sessionTarget current using persisted sessionKey context", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-1" },
		{ "name", "nightly" },
		{ "sessionKey", "agent:main:persisted" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "ping" } } },
		{ "state", CronJson::object() }
	};

	const CronJson patch = {
		{ "sessionTarget", "current" }
	};

	CronNormalize::ApplyPatch(job, patch);
	REQUIRE(job.value("sessionTarget", std::string()) == "session:agent:main:persisted");
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
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"enabled\":false,\"failureAlert\":{\"after\":2,\"cooldownMs\":0,\"mode\":\"announce\"}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron update validator rejects patch failureAlert non-object non-boolean", "[cron][schema]") {
	const RequestFrame request{
		.id = "3u-fa-type",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"failureAlert\":\"bad\"}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_type");
	REQUIRE(issue.message.find("params.patch.failureAlert") != std::string::npos);
}

TEST_CASE("Cron update validator rejects patch failureAlert after non-integer", "[cron][schema]") {
	const RequestFrame request{
		.id = "3u-fa-after-float",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"failureAlert\":{\"after\":1.5}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.patch.failureAlert.after") != std::string::npos);
}

TEST_CASE("Cron update validator rejects patch failureAlert cooldownMs negative", "[cron][schema]") {
	const RequestFrame request{
		.id = "3u-fa-cooldown-negative",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"failureAlert\":{\"cooldownMs\":-1}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.patch.failureAlert.cooldownMs") != std::string::npos);
}

TEST_CASE("Cron update validator rejects patch failureAlert invalid mode", "[cron][schema]") {
	const RequestFrame request{
		.id = "3u-fa-mode-invalid",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"failureAlert\":{\"mode\":\"none\"}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.patch.failureAlert.mode") != std::string::npos);
}

TEST_CASE("Cron update validator rejects patch failureAlert unknown nested field", "[cron][schema]") {
	const RequestFrame request{
		.id = "3u-fa-extra",
		.method = "cron.update",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"patch\":{\"failureAlert\":{\"after\":2,\"extra\":true}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.patch.failureAlert.extra") != std::string::npos);
}

TEST_CASE("Cron add validator rejects non-object non-boolean failureAlert", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-type",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":\"yes\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_type");
	REQUIRE(issue.message.find("params.failureAlert") != std::string::npos);
}

TEST_CASE("Cron add validator rejects failureAlert after with non-integer value", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-after-float",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":{\"after\":1.5}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.failureAlert.after") != std::string::npos);
}

TEST_CASE("Cron add validator rejects failureAlert cooldownMs below zero", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-cooldown-negative",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":{\"cooldownMs\":-1}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.failureAlert.cooldownMs") != std::string::npos);
}

TEST_CASE("Cron add validator rejects unsupported failureAlert mode", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-mode-invalid",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":{\"mode\":\"none\"}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.failureAlert.mode") != std::string::npos);
}

TEST_CASE("Cron add validator rejects unknown nested failureAlert field", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-extra-field",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":{\"after\":2,\"extra\":true}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.failureAlert.extra") != std::string::npos);
}

TEST_CASE("Cron add validator accepts structured failureAlert object", "[cron][schema]") {
	const RequestFrame request{
		.id = "3c-structured",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"failureAlert\":{\"after\":2,\"cooldownMs\":1000,\"mode\":\"announce\",\"channel\":\"last\",\"to\":\"target\",\"accountId\":\"acc-1\"}}")
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

TEST_CASE("Cron add validator rejects webhook delivery target without http scheme", "[cron][schema]") {
	const RequestFrame request{
		.id = "3d-http",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"delivery\":{\"mode\":\"webhook\",\"to\":\"example.test/hook\"}}")
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

TEST_CASE("Cron add validator rejects webhook failureDestination target without http scheme", "[cron][schema]") {
	const RequestFrame request{
		.id = "3e-http",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"name\":\"job\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"delivery\":{\"mode\":\"announce\",\"failureDestination\":{\"mode\":\"webhook\",\"to\":\"example.test/failure\"}}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.delivery.failureDestination.to") != std::string::npos);
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

TEST_CASE("Cron runs validator accepts unknown deliveryStatuses entry", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-bad-delivery-statuses",
		.method = "cron.runs",
		.paramsJson = std::string("{\"deliveryStatuses\":[\"unknown\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron runs validator accepts unknown deliveryStatus scalar", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-unknown-delivery-status",
		.method = "cron.runs",
		.paramsJson = std::string("{\"deliveryStatus\":\"unknown\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron runs validator rejects id with path separator", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-id-path-separator",
		.method = "cron.runs",
		.paramsJson = std::string("{\"scope\":\"job\",\"id\":\"job/a\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects jobId with path separator", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-jobid-path-separator",
		.method = "cron.runs",
		.paramsJson = std::string("{\"scope\":\"job\",\"jobId\":\"job\\\\a\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.jobId") != std::string::npos);
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

TEST_CASE("Cron timer schedules retry for transient webhook delivery failure", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-webhook-transient" },
			{ "name", "webhook transient" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://example.test/hook" },
					{ "simulateTransientFailure", true }
				} },
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
	REQUIRE(runs[0].value("errorCategory", std::string()) == "network");
	REQUIRE(runs[0].value("retryScheduled", false));
	REQUIRE(runs[0].value("retryAttempt", 0) == 1);
	REQUIRE(jobs[0]["state"].value("retryPendingUntilMs", static_cast<std::int64_t>(0)) == nowMs + 5'000);
	REQUIRE(jobs[0]["state"].value("nextRunAtMs", static_cast<std::int64_t>(0)) == nowMs + 5'000);
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

TEST_CASE("Cron run validator accepts id and mode force", "[cron][schema]") {
	const RequestFrame request{
		.id = "run-valid-force",
		.method = "cron.run",
		.paramsJson = std::string("{\"id\":\"cron-1\",\"mode\":\"force\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
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
	REQUIRE(runs[0].value("deliveryMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "invalid-url");
	REQUIRE(runs[0].value("deliveryAttempted", false));
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
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "invalid-destination" },
							{ "channel", "alerts" },
							{ "accountId", "acc-failure" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "invalid-destination");
	REQUIRE(runs[0].value("failureDestinationChannel", std::string()) == "alerts");
	REQUIRE(runs[0].value("failureDestinationAccountId", std::string()) == "acc-failure");
	REQUIRE(runs[0].value("failureDestinationAttempted", false));
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "invalid failure destination webhook target");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationStatus", std::string()) == "not-delivered");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationChannel", std::string()) == "alerts");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationAccountId", std::string()) == "acc-failure");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationAttempted", false));
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

TEST_CASE("Cron timer suppresses webhook failure destination when target matches primary webhook target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-webhook-target-match" },
			{ "name", "failure destination webhook target match" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://example.test/hook" },
					{ "simulateTransientFailure", true },
					{ "channel", "last" },
					{ "accountId", "acc-primary" },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "https://example.test/hook" },
							{ "channel", "secondary" },
							{ "accountId", "acc-failure" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://example.test/hook");
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "failure destination matches primary delivery target");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationError", std::string()) == "failure destination matches primary delivery target");
}

TEST_CASE("Cron timer does not suppress webhook failure destination when webhook target differs", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-webhook-target-different" },
			{ "name", "failure destination webhook target different" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://example.test/hook-primary" },
					{ "simulateTransientFailure", true },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "https://example.test/hook-failure" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
	REQUIRE(runs[0]["failureDestinationHttpStatus"].is_null());
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://example.test/hook-failure");
	REQUIRE(runs[0].value("failureDestinationAttempted", false));
	const bool noFailureDestinationError =
		!runs[0].contains("failureDestinationError") ||
		runs[0]["failureDestinationError"].is_null() ||
		runs[0].value("failureDestinationError", std::string()).empty();
	REQUIRE(noFailureDestinationError);
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationStatus", std::string()) == "delivered");
}

TEST_CASE("Cron timer records webhook HTTP status and retries on transient HTTP codes", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	SECTION("HTTP 503 is retryable and records deliveryHttpStatus") {
		CronJson jobs = CronJson::array({
			{
				{ "id", "job-webhook-http-503" },
				{ "name", "webhook http 503" },
				{ "enabled", true },
				{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
				{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
				{ "delivery",
					{
						{ "mode", "webhook" },
						{ "to", "https://example.test/hook" },
						{ "simulateHttpStatus", 503 }
					} },
				{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 5'000 }) } } },
				{ "state", { { "nextRunAtMs", nowMs - 1 } } }
			}
		});
		CronJson runs = CronJson::array();

		timer.PumpDueRuns(jobs, runs, nowMs, false);
		REQUIRE(runs.size() == 1);
		REQUIRE(runs[0].value("status", std::string()) == "error");
		REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
		REQUIRE(runs[0].value("deliveryHttpStatus", static_cast<std::int64_t>(0)) == 503);
		REQUIRE(runs[0].value("retryScheduled", false));
		REQUIRE(runs[0].value("retryAttempt", 0) == 1);
		REQUIRE(jobs[0]["state"].value("lastDeliveryHttpStatus", static_cast<std::int64_t>(0)) == 503);
	}

	SECTION("HTTP 400 is non-retryable and records deliveryHttpStatus") {
		CronJson jobs = CronJson::array({
			{
				{ "id", "job-webhook-http-400" },
				{ "name", "webhook http 400" },
				{ "enabled", true },
				{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
				{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
				{ "delivery",
					{
						{ "mode", "webhook" },
						{ "to", "https://example.test/hook" },
						{ "simulateHttpStatus", 400 }
					} },
				{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 5'000 }) } } },
				{ "state", { { "nextRunAtMs", nowMs - 1 } } }
			}
		});
		CronJson runs = CronJson::array();

		timer.PumpDueRuns(jobs, runs, nowMs, false);
		REQUIRE(runs.size() == 1);
		REQUIRE(runs[0].value("status", std::string()) == "error");
		REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
		REQUIRE(runs[0].value("deliveryHttpStatus", static_cast<std::int64_t>(0)) == 400);
		REQUIRE_FALSE(runs[0].value("retryScheduled", true));
		REQUIRE(runs[0].value("retryAttempt", 1) == 0);
		REQUIRE(jobs[0]["state"]["retryPendingUntilMs"].is_null());
		REQUIRE(jobs[0]["state"].value("lastDeliveryHttpStatus", static_cast<std::int64_t>(0)) == 400);
	}
}

TEST_CASE("Cron timer records failure destination webhook HTTP status", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-http-502" },
			{ "name", "failure destination webhook http status" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "https://example.test/failure" },
							{ "simulateHttpStatus", 502 }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationHttpStatus", static_cast<std::int64_t>(0)) == 502);
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationHttpStatus", static_cast<std::int64_t>(0)) == 502);
}

TEST_CASE("Cron timer transport dispatch classifies webhook transport failures", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	SECTION("Primary webhook transport dispatch failure is retryable network error") {
		CronJson jobs = CronJson::array({
			{
				{ "id", "job-webhook-transport-dispatch-fail" },
				{ "name", "webhook transport dispatch fail" },
				{ "enabled", true },
				{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
				{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
				{ "delivery",
					{
						{ "mode", "webhook" },
						{ "to", "http:///" },
						{ "transportDispatch", true }
					} },
				{ "state", { { "nextRunAtMs", nowMs - 1 } } }
			}
		});
		CronJson runs = CronJson::array();

		timer.PumpDueRuns(jobs, runs, nowMs, false);
		REQUIRE(runs.size() == 1);
		REQUIRE(runs[0].value("status", std::string()) == "error");
		REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
		REQUIRE(runs[0].value("errorCategory", std::string()) == "network");
		REQUIRE(runs[0].value("deliveryAttempted", true) == false);
		REQUIRE(runs[0].value("summary", std::string()) == "Webhook delivery transport dispatch failed");
	}

	SECTION("Failure destination webhook transport dispatch failure is recorded") {
		CronJson jobs = CronJson::array({
			{
				{ "id", "job-failure-destination-transport-dispatch-fail" },
				{ "name", "failure destination transport dispatch fail" },
				{ "enabled", true },
				{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
				{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
				{ "delivery",
					{
						{ "mode", "webhook" },
						{ "to", "invalid-url" },
						{ "failureDestination",
							{
								{ "mode", "webhook" },
								{ "to", "http:///" },
								{ "transportDispatch", true }
							} }
					} },
				{ "state", { { "nextRunAtMs", nowMs - 1 } } }
			}
		});
		CronJson runs = CronJson::array();

		timer.PumpDueRuns(jobs, runs, nowMs, false);
		REQUIRE(runs.size() == 1);
		REQUIRE(runs[0].value("status", std::string()) == "error");
		REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-delivered");
		REQUIRE(
			runs[0].value("failureDestinationError", std::string()) ==
			"failed to parse webhook URL");
		REQUIRE(runs[0].value("failureDestinationAttempted", true) == false);
	}
}

TEST_CASE("Cron timer marks announce failure destination empty target as not-delivered", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-empty" },
			{ "name", "announce failure destination empty" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "failureDestination", { { "mode", "announce" }, { "to", "" } } }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "announce failure destination target is empty");
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
	REQUIRE(runs[0].value("failureAlertTarget", std::string()) == "bad-target");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "bad-target");
}

TEST_CASE("Cron timer falls back failureAlert announce target to delivery target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-announce-fallback" },
			{ "name", "alert announce fallback" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/hook" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "announce" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureAlertTarget", std::string()) == "https://alerts.example/hook");
	REQUIRE(runs[0].value("failureAlertChannel", std::string()) == "last");
	REQUIRE(runs[0]["failureAlertAccountId"].is_null());
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example/hook");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertChannel", std::string()) == "last");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAccountId"].is_null());
}

TEST_CASE("Cron timer falls back failureAlert announce channel/account to delivery", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-announce-channel-account-fallback" },
			{ "name", "alert announce channel account fallback" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "channel", "alerts" },
					{ "accountId", "acc-delivery" }
				} },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "announce" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureAlertChannel", std::string()) == "alerts");
	REQUIRE(runs[0].value("failureAlertAccountId", std::string()) == "acc-delivery");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertChannel", std::string()) == "alerts");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAccountId", std::string()) == "acc-delivery");
}

TEST_CASE("Cron timer marks timeout lifecycle fields", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-timeout" },
			{ "name", "timeout job" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "ping" }, { "timeoutSeconds", 1 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("timedOut", false));
	REQUIRE(runs[0].value("aborted", true) == false);
	REQUIRE(runs[0].value("errorCategory", std::string()) == "timeout");
	REQUIRE(jobs[0]["state"].value("lastRunTimedOut", false));
	REQUIRE(jobs[0]["state"].value("lastRunAborted", true) == false);
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
