#include "pch.h"

#include "../src/cron/CronJsonCompat.h"
#include "../src/cron/CronModels.h"
#include "../src/cron/CronNormalize.h"
#include "../src/cron/CronOpsService.h"
#include "../src/cron/CronOpsServiceTestHooks.h"
#include "../src/cron/CronStoreService.h"
#include "../src/cron/CronTimerService.h"
#include "../src/gateway/GatewayHost.h"
#include "../src/gateway/GatewayProtocolSchemaValidator.h"
#include "../src/gateway/GatewayTestHooks.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
	using blazeclaw::cron::CronJson;
	using blazeclaw::cron::IsUsableJsonDocument;
	using blazeclaw::cron::ParseJsonWithJson5Fallback;
	using blazeclaw::cron::ParseJsonStreamWithJson5Fallback;
	using blazeclaw::cron::CronNormalize;
	using blazeclaw::cron::CronOpsService;
	using blazeclaw::cron::CronPumpOptions;
	using blazeclaw::cron::CronSchedulerConfig;
	using blazeclaw::cron::CronRealtimeEvent;
	using blazeclaw::cron::CronScheduleNotificationEvent;
	using blazeclaw::cron::CronStoreService;
	using blazeclaw::cron::CronTimerService;
	using blazeclaw::cron::kCronMinRefireGapMs;
	using blazeclaw::cron::kCronStuckRunMs;
	using blazeclaw::cron::kWakeModeNextHeartbeat;
	using blazeclaw::gateway::GatewayHost;
	using blazeclaw::gateway::protocol::GatewayProtocolSchemaValidator;
	using blazeclaw::gateway::protocol::RequestFrame;
	using blazeclaw::gateway::protocol::ResponseFrame;
	using blazeclaw::gateway::protocol::SchemaValidationIssue;

	nlohmann::json ParseGatewayFrame(const std::string& frameJson) {
		return nlohmann::json::parse(frameJson);
	}

	struct IsolatedCronOpsFixture {
		std::filesystem::path root;
		std::optional<CronOpsService> service;

		explicit IsolatedCronOpsFixture(const char* label)
			: root(
				std::filesystem::temp_directory_path() /
				("blazeclaw-cron-ops-" + std::string(label))) {
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
			std::filesystem::create_directories(root, ec);
			service.emplace(root / "cron.jobs.json", root / "cron.runs.json");
		}

		~IsolatedCronOpsFixture() {
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
		}

		CronOpsService& ops() {
			return *service;
		}
	};

	blazeclaw::gateway::protocol::ResponseFrame RouteGatewayCron(
		GatewayHost& host,
		const std::string& id,
		const std::string& method,
		std::optional<std::string> paramsJson = std::nullopt) {
		return host.RouteRequest(
			RequestFrame{
				.id = id,
				.method = method,
				.paramsJson = std::move(paramsJson),
			});
	}

	bool ValidateGatewayCronRunsResponse(const ResponseFrame& response) {
		if (!response.ok || !response.payloadJson.has_value()) {
			return false;
		}

		const CronJson payload = CronJson::parse(response.payloadJson.value());
		if (!payload.is_object() || !payload.contains("entries") ||
			!payload["entries"].is_array()) {
			return false;
		}

		SchemaValidationIssue issue{};
		for (const auto& entry : payload["entries"]) {
			const CronJson wrapped = {
				{ "entries", CronJson::array({ entry }) },
				{ "total", 1 },
				{ "limit", 1 },
				{ "offset", 0 },
				{ "nextOffset", nullptr },
				{ "hasMore", false }
			};
			const ResponseFrame singleEntryResponse{
				.id = response.id,
				.ok = true,
				.payloadJson = wrapped.dump(),
				.error = std::nullopt,
			};
			if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(
				"cron.runs",
				singleEntryResponse,
				issue) ||
				!issue.code.empty()) {
				return false;
			}
		}

		const CronJson envelopeOnly = {
			{ "entries", CronJson::array() },
			{ "total", payload.value("total", static_cast<std::int64_t>(0)) },
			{ "limit", payload.value("limit", static_cast<std::int64_t>(0)) },
			{ "offset", payload.value("offset", static_cast<std::int64_t>(0)) },
			{ "nextOffset", payload.contains("nextOffset") ? payload["nextOffset"] : CronJson(nullptr) },
			{ "hasMore", payload.value("hasMore", false) }
		};
		const ResponseFrame envelopeResponse{
			.id = response.id,
			.ok = true,
			.payloadJson = envelopeOnly.dump(),
			.error = std::nullopt,
		};
		issue = {};
		return GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"cron.runs",
			envelopeResponse,
			issue) &&
			issue.code.empty();
	}

	bool ValidateGatewayCronResponse(
		const std::string& method,
		const ResponseFrame& response) {
		if (!response.ok || !response.payloadJson.has_value()) {
			return false;
		}

		if (method == "cron.runs") {
			return ValidateGatewayCronRunsResponse(response);
		}

		SchemaValidationIssue issue{};
		return GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			method,
			response,
			issue) &&
			issue.code.empty();
	}

	struct GatewayCronProductionFixture {
		std::filesystem::path root;
		std::unique_ptr<GatewayHost> host;

		explicit GatewayCronProductionFixture(const char* label)
			: root(
				std::filesystem::temp_directory_path() /
				("blazeclaw-cron-gw-" + std::string(label))) {
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
			std::filesystem::create_directories(root, ec);
			blazeclaw::cron::test_hooks::ConfigureCronOpsServiceForTest(
				root / "cron.jobs.json",
				root / "cron.runs.json");
			host = std::make_unique<GatewayHost>();
			if (!host->StartLocalDispatchOnly()) {
				throw std::runtime_error("failed to start gateway local dispatch");
			}
			blazeclaw::gateway::test_hooks::WireCronProductionIntegrationForTest(*host);
		}

		~GatewayCronProductionFixture() {
			host.reset();
			blazeclaw::cron::test_hooks::ResetCronOpsServiceForTest();
			std::error_code ec;
			std::filesystem::remove_all(root, ec);
		}

		GatewayHost& gateway() {
			return *host;
		}
	};

TEST_CASE("Cron runs response validator rejects non-boolean heartbeat fallback marker", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-runs-heartbeat-fallback-bad-type",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"failed\",\"heartbeatFallbackWakeRequested\":\"yes\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}
}

TEST_CASE("Cron timer accepts webhook url alias when delivery.to is omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-webhook-url-alias" },
			{ "name", "webhook url alias" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", { { "mode", "webhook" }, { "url", "https://example.test/hook" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed =
		timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://example.test/hook");
}

TEST_CASE("Cron timer accepts webhook url alias for failure destination", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-webhook-failure-url-alias" },
			{ "name", "webhook failure url alias" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-target" },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "url", "https://example.test/failure" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed =
		timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://example.test/failure");
}

TEST_CASE("Cron add validator accepts nullable identity fields and flattened payload fields", "[cron][schema]") {
	const RequestFrame request{
		.id = "1-nullable-flattened",
		.method = "cron.add",
		.paramsJson = std::string(
			"{\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"message\":\"nightly ping\",\"agentId\":null,\"sessionKey\":null}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron add validator enforces nested schedule kind constraints", "[cron][schema]") {
	SECTION("rejects every schedule when everyMs is missing") {
		const RequestFrame request{
			.id = "cron-add-schedule-every-missing-everyms",
			.method = "cron.add",
			.paramsJson = std::string(
				"{\"schedule\":{\"kind\":\"every\"},\"message\":\"nightly ping\"}")
		};

		SchemaValidationIssue issue{};
		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code == "schema_missing_field");
		REQUIRE(issue.message.find("params.schedule.everyMs") != std::string::npos);
	}

	SECTION("rejects cron schedule when expr and cron aliases are both missing") {
		const RequestFrame request{
			.id = "cron-add-schedule-cron-missing-expr",
			.method = "cron.add",
			.paramsJson = std::string(
				"{\"schedule\":{\"kind\":\"cron\"},\"text\":\"heartbeat\"}")
		};

		SchemaValidationIssue issue{};
		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code == "schema_missing_field");
		REQUIRE(issue.message.find("params.schedule.expr") != std::string::npos);
	}

	SECTION("accepts cron schedule with expr and tz") {
		const RequestFrame request{
			.id = "cron-add-schedule-cron-with-tz",
			.method = "cron.add",
			.paramsJson = std::string(
				"{\"schedule\":{\"kind\":\"cron\",\"expr\":\"*/5 * * * *\",\"tz\":\"+08:00\"},\"text\":\"heartbeat\"}")
		};

		SchemaValidationIssue issue{};
		REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code.empty());
	}
}

TEST_CASE("Cron update validator enforces nested patch schedule constraints", "[cron][schema]") {
	SECTION("rejects non-object schedule patch") {
		const RequestFrame request{
			.id = "cron-update-schedule-not-object",
			.method = "cron.update",
			.paramsJson = std::string(
				"{\"id\":\"cron-1\",\"patch\":{\"schedule\":\"every\"}}")
		};

		SchemaValidationIssue issue{};
		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code == "schema_invalid_type");
		REQUIRE(issue.message.find("params.patch.schedule") != std::string::npos);
	}

	SECTION("rejects cron patch with negative staggerMs") {
		const RequestFrame request{
			.id = "cron-update-schedule-negative-stagger",
			.method = "cron.update",
			.paramsJson = std::string(
				"{\"id\":\"cron-1\",\"patch\":{\"schedule\":{\"kind\":\"cron\",\"expr\":\"*/10 * * * *\",\"staggerMs\":-1}}}")
		};

		SchemaValidationIssue issue{};
		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code == "schema_invalid_value");
		REQUIRE(issue.message.find("params.patch.schedule.staggerMs") != std::string::npos);
	}

	SECTION("accepts at patch with atMs integer") {
		const RequestFrame request{
			.id = "cron-update-schedule-at-atms",
			.method = "cron.update",
			.paramsJson = std::string(
				"{\"id\":\"cron-1\",\"patch\":{\"schedule\":{\"kind\":\"at\",\"atMs\":1700000000000}}}")
		};

		SchemaValidationIssue issue{};
		REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
		REQUIRE(issue.code.empty());
	}
}

TEST_CASE("Cron runs validator rejects mixed status and statuses filters", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-status-and-statuses-mixed",
		.method = "cron.runs",
		.paramsJson = std::string("{\"status\":\"running\",\"statuses\":[\"queued\",\"failed\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.status") != std::string::npos);
}

TEST_CASE("Cron runs validator rejects mixed deliveryStatus and deliveryStatuses filters", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-delivery-status-and-delivery-statuses-mixed",
		.method = "cron.runs",
		.paramsJson = std::string("{\"deliveryStatus\":\"delivered\",\"deliveryStatuses\":[\"suppressed\",\"unknown\"]}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_params");
	REQUIRE(issue.message.find("params.deliveryStatus") != std::string::npos);
}

TEST_CASE("Cron normalize add builds payload from flattened fields", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "message", "run nightly sync" },
		{ "model", "gpt-4.1" }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("payload"));
	REQUIRE(normalized["payload"].is_object());
	REQUIRE(normalized["payload"].value("kind", std::string()) == "agentTurn");
	REQUIRE(normalized["payload"].value("message", std::string()) == "run nightly sync");
	REQUIRE(normalized["payload"].value("model", std::string()) == "gpt-4.1");
	REQUIRE_FALSE(normalized.contains("message"));
	REQUIRE_FALSE(normalized.contains("model"));
}

TEST_CASE("Cron normalize infers webhook failureDestination mode from url alias when omitted", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", {
			{ "mode", "webhook" },
			{ "to", "https://example.test/primary" },
			{ "failureDestination", {
				{ "url", "https://example.test/failure" }
			} }
		} }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("delivery"));
	REQUIRE(normalized["delivery"].is_object());
	REQUIRE(normalized["delivery"].contains("failureDestination"));
	REQUIRE(normalized["delivery"]["failureDestination"].is_object());
	REQUIRE(
		normalized["delivery"]["failureDestination"].value("mode", std::string()) ==
		"webhook");
}

TEST_CASE("Cron normalize infers webhook delivery mode from url alias when omitted", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", {
			{ "url", "https://example.test/primary" }
		} }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("delivery"));
	REQUIRE(normalized["delivery"].is_object());
	REQUIRE(normalized["delivery"].value("mode", std::string()) == "webhook");
}

TEST_CASE("Cron gateway pre-validator normalization canonicalizes flat cron.add params", "[cron][gateway][normalize]") {
	GatewayHost host;

	const std::string frame = host.HandleInboundText(
		"{"
		"\"type\":\"req\","
		"\"id\":\"cron-add-flat\","
		"\"method\":\"cron.add\","
		"\"params\":{"
		"\"kind\":\"every\","
		"\"everyMs\":60000,"
		"\"message\":\"nightly ping\","
		"\"model\":\"gpt-4.1\","
		"\"deliveryMode\":\"webhook\","
		"\"deliveryTo\":\"https://example.test/hook\","
		"\"failureDestinationMode\":\"announce\","
		"\"failureDestinationTo\":\"ops-room\""
		"}"
		"}");

	const nlohmann::json response = ParseGatewayFrame(frame);
	REQUIRE(response.value("type", std::string()) == "res");
	REQUIRE(response.value("id", std::string()) == "cron-add-flat");
	if (response.value("ok", false)) {
		REQUIRE(response.contains("payload"));
		REQUIRE(response["payload"].is_object());
		REQUIRE(response["payload"].contains("id"));
	}
	else {
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("cron.add canonicalizes flat webhook url aliases to nested to fields") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-add-flat-url-aliases\","
				"\"method\":\"cron.add\","
				"\"params\":{"
				"\"kind\":\"every\","
				"\"everyMs\":60000,"
				"\"text\":\"nightly ping\","
				"\"deliveryMode\":\"webhook\","
				"\"deliveryUrl\":\"https://example.test/delivery-url\","
				"\"failureDestinationMode\":\"webhook\","
				"\"failureDestinationUrl\":\"https://example.test/failure-url\","
				"\"failureAlertMode\":\"webhook\","
				"\"failureAlertUrl\":\"https://example.test/alert-url\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-add-flat-url-aliases");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}
}

TEST_CASE("Cron gateway pre-validator normalization canonicalizes flat update/run/runs/wake params", "[cron][gateway][normalize]") {
	GatewayHost host;

	SECTION("cron.update lifts flat patch fields and cronId alias") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-update-flat\","
				"\"method\":\"cron.update\","
				"\"params\":{"
				"\"cronId\":\"cron-1\","
				"\"enabled\":false,"
				"\"failureAlert\":false"
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-update-flat");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
	}

	SECTION("cron.update canonicalizes flat schedule payload and delivery aliases") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\"," 
				"\"id\":\"cron-update-flat-shape\"," 
				"\"method\":\"cron.update\"," 
				"\"params\":{"
				"\"cronId\":\"cron-1\"," 
				"\"kind\":\"every\"," 
				"\"everyMs\":60000," 
				"\"message\":\"nightly sweep\"," 
				"\"deliveryMode\":\"webhook\"," 
				"\"deliveryTo\":\"https://example.test/update-hook\"," 
				"\"failureDestinationMode\":\"announce\"," 
				"\"failureDestinationTo\":\"ops-room\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-update-flat-shape");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("cron.update patch canonicalizes flat webhook url aliases to nested to fields") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-update-flat-url-aliases\","
				"\"method\":\"cron.update\","
				"\"params\":{"
				"\"id\":\"cron-1\","
				"\"patch\":{"
				"\"deliveryMode\":\"webhook\","
				"\"deliveryUrl\":\"https://example.test/update-delivery-url\","
				"\"failureDestinationMode\":\"webhook\","
				"\"failureDestinationUrl\":\"https://example.test/update-failure-url\","
				"\"failureAlertMode\":\"webhook\","
				"\"failureAlertUrl\":\"https://example.test/update-alert-url\""
				"}"
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-update-flat-url-aliases");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("cron.update canonicalizes aliases inside explicit patch object") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\"," 
				"\"id\":\"cron-update-patch-flat\"," 
				"\"method\":\"cron.update\"," 
				"\"params\":{"
				"\"id\":\"cron-1\"," 
				"\"patch\":{"
				"\"kind\":\"cron\"," 
				"\"expr\":\"*/5 * * * *\"," 
				"\"text\":\"refresh cache\"," 
				"\"deliveryMode\":\"webhook\"," 
				"\"deliveryTo\":\"https://example.test/patch-hook\""
				"}"
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-update-patch-flat");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("cron.run maps cronId alias to id") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-run-flat\","
				"\"method\":\"cron.run\","
				"\"params\":{"
				"\"cronId\":\"cron-1\","
				"\"mode\":\"force\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-run-flat");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
	}

	SECTION("cron.run maps jobId alias to id") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-run-jobid\","
				"\"method\":\"cron.run\","
				"\"params\":{"
				"\"jobId\":\"cron-1\","
				"\"mode\":\"force\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-run-jobid");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
	}

	SECTION("cron.remove maps jobId alias to id") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-remove-jobid\","
				"\"method\":\"cron.remove\","
				"\"params\":{"
				"\"jobId\":\"cron-1\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-remove-jobid");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
	}

	SECTION("cron.runs preserves job scope when jobId alias is provided") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-runs-jobid-scope\","
				"\"method\":\"cron.runs\","
				"\"params\":{"
				"\"scope\":\"job\","
				"\"jobId\":\"cron-1\","
				"\"limit\":20,"
				"\"offset\":0"
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-runs-jobid-scope");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].contains("entries"));
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("cron.runs normalizes string list aliases and invalid job scope") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-runs-flat\","
				"\"method\":\"cron.runs\","
				"\"params\":{"
				"\"scope\":\"job\","
				"\"statuses\":\"ok\","
				"\"deliveryStatuses\":\"delivered\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-runs-flat");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].contains("entries"));
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("cron.runs normalizes csv aliases into array filters") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-runs-csv-alias\","
				"\"method\":\"cron.runs\","
				"\"params\":{"
				"\"statuses\":\"ok, failed\","
				"\"deliveryStatuses\":\"delivered,not-delivered\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-runs-csv-alias");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].contains("entries"));
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("cron.runs normalizes singular csv aliases into array filters") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-runs-singular-csv-alias\","
				"\"method\":\"cron.runs\","
				"\"params\":{"
				"\"status\":\"ok, failed\","
				"\"deliveryStatus\":\"delivered,not-delivered\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-runs-singular-csv-alias");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].contains("entries"));
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("cron.add lifts flat failureAlert aliases into failureAlert object") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-add-flat-failure-alert\","
				"\"method\":\"cron.add\","
				"\"params\":{"
				"\"kind\":\"every\","
				"\"everyMs\":60000,"
				"\"text\":\"nightly ping\","
				"\"failureAlertAfter\":2,"
				"\"failureAlertCooldownMs\":120000,"
				"\"failureAlertMode\":\"announce\","
				"\"failureAlertTo\":\"ops-room\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-add-flat-failure-alert");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("cron.update patch lifts flat failureAlert aliases") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"cron-update-flat-failure-alert\","
				"\"method\":\"cron.update\","
				"\"params\":{"
				"\"id\":\"cron-1\","
				"\"patch\":{"
				"\"failureAlertAfter\":3,"
				"\"failureAlertCooldownMs\":180000,"
				"\"failureAlertMode\":\"announce\","
				"\"failureAlertTo\":\"ops-room\""
				"}"
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "cron-update-flat-failure-alert");
		REQUIRE_FALSE(response.value("ok", true));
		REQUIRE(response.contains("error"));
		REQUIRE(response["error"].is_object());
		REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
		REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
	}

	SECTION("wake maps wakeMode alias to mode") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"wake-flat\","
				"\"method\":\"wake\","
				"\"params\":{"
				"\"wakeMode\":\"now\","
				"\"text\":\"wake up\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "wake-flat");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].value("mode", std::string()) == "now");
			REQUIRE(response["payload"].value("text", std::string()) == "wake up");
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("wake canonicalizes legacy nextHeartbeat mode alias") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"wake-next-heartbeat-alias\","
				"\"method\":\"wake\","
				"\"params\":{"
				"\"wakeMode\":\"nextHeartbeat\","
				"\"text\":\"refresh\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "wake-next-heartbeat-alias");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].value("mode", std::string()) == "next-heartbeat");
			REQUIRE(response["payload"].value("text", std::string()) == "refresh");
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}

	SECTION("wake canonicalizes mode casing and spacing to strict taxonomy") {
		const nlohmann::json response = ParseGatewayFrame(
			host.HandleInboundText(
				"{"
				"\"type\":\"req\","
				"\"id\":\"wake-mode-casing-spacing\","
				"\"method\":\"wake\","
				"\"params\":{"
				"\"mode\":\"  NEXT-HEARTBEAT  \","
				"\"text\":\"refresh\""
				"}"
				"}"));

		REQUIRE(response.value("type", std::string()) == "res");
		REQUIRE(response.value("id", std::string()) == "wake-mode-casing-spacing");
		if (response.value("ok", false)) {
			REQUIRE(response.contains("payload"));
			REQUIRE(response["payload"].is_object());
			REQUIRE(response["payload"].value("mode", std::string()) == "next-heartbeat");
			REQUIRE(response["payload"].value("text", std::string()) == "refresh");
		}
		else {
			REQUIRE(response.contains("error"));
			REQUIRE(response["error"].is_object());
			REQUIRE(response["error"].value("code", std::string()) != "schema_missing_field");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_params");
			REQUIRE(response["error"].value("code", std::string()) != "schema_invalid_type");
		}
	}
}

TEST_CASE("Cron timer computes deterministic stagger offset for the same job", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	const CronJson job = {
		{ "id", "cron-deterministic-stagger" },
		{ "enabled", true },
		{ "schedule",
			{
				{ "kind", "cron" },
				{ "expr", "* * * * *" },
				{ "staggerMs", 60'000 }
			} },
		{ "state", CronJson::object() }
	};

	const auto first = timer.ComputeNextRunAtMs(job, nowMs);
	const auto second = timer.ComputeNextRunAtMs(job, nowMs);
	REQUIRE(first.has_value());
	REQUIRE(second.has_value());
	REQUIRE(first.value() == second.value());
	REQUIRE(first.value() > nowMs);
}

TEST_CASE("Cron timer staggered cron schedule preserves due minute slot via shifted cursor", "[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson job = {
		{ "id", "cron-shifted-cursor-minute" },
		{ "enabled", true },
		{ "schedule",
			{
				{ "kind", "cron" },
				{ "expr", "* * * * *" },
				{ "staggerMs", 59'000 }
			} },
		{ "state", CronJson::object() }
	};

	const auto next = timer.ComputeNextRunAtMs(job, nowMs);
	REQUIRE(next.has_value());
	REQUIRE(next.value() > nowMs);
	REQUIRE((next.value() - nowMs) <= 60'000);
}

TEST_CASE("Cron timer rejects cron schedule missing expression field", "[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-cron-missing-expr" },
			{ "name", "cron missing expr" },
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", CronJson::object() }
		}
	});

	for (int i = 0; i < 3; ++i) {
		timer.RecomputeSchedules(jobs, nowMs + (i * 1'000));
	}

	REQUIRE(jobs[0].value("enabled", true) == false);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].is_object());
	REQUIRE(jobs[0]["state"].value("scheduleErrorCount", 0) >= 3);
	REQUIRE(
		jobs[0]["state"].value("lastError", std::string()).find("invalid cron expression field count") !=
		std::string::npos);
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabled", false));
}

TEST_CASE("Cron timer maintenance recompute preserves due slot when configured", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	const std::int64_t dueAtMs = nowMs - 5'000;
	CronJson jobs = CronJson::array({
		{
			{ "id", "cron-preserve-due" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 }, { "anchorMs", nowMs - 120'000 } } },
			{ "state", { { "nextRunAtMs", dueAtMs } } }
		}
	});

	blazeclaw::cron::CronRecomputeOptions options;
	options.preserveDueSlots = true;
	const bool changed = timer.RecomputeSchedules(jobs, nowMs, options);
	REQUIRE_FALSE(changed);
	REQUIRE(jobs[0]["state"].value("nextRunAtMs", static_cast<std::int64_t>(0)) == dueAtMs);
}

TEST_CASE("Cron timer normalizes every anchor during schedule recompute", "[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	const std::int64_t createdAtMs = nowMs - 120'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-every-anchor-normalize" },
			{ "enabled", true },
			{ "createdAtMs", createdAtMs },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", CronJson::object() }
		}
	});

	const bool changed = timer.RecomputeSchedules(jobs, nowMs);
	REQUIRE(changed);
	REQUIRE(jobs[0]["schedule"].value("anchorMs", static_cast<std::int64_t>(-1)) == createdAtMs);
	REQUIRE(jobs[0]["state"].contains("nextRunAtMs"));
	REQUIRE(jobs[0]["state"]["nextRunAtMs"].is_number_integer());
}

TEST_CASE("Cron timer clears invalid at schedule without synthetic fallback", "[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-at-invalid-shape" },
			{ "name", "at invalid shape" },
			{ "enabled", true },
			{ "schedule", { { "kind", "at" } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", { { "nextRunAtMs", nowMs + 60'000 } } }
		}
	});

	const bool changed = timer.RecomputeSchedules(jobs, nowMs);
	REQUIRE(changed);
	REQUIRE(jobs[0]["state"].contains("nextRunAtMs"));
	REQUIRE(jobs[0]["state"]["nextRunAtMs"].is_null());
	REQUIRE_FALSE(jobs[0]["state"].contains("scheduleErrorCount"));
}

TEST_CASE("Cron normalize add accepts ISO schedule.at and sets atMs", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "at" }, { "at", "2026-05-17T12:34:56Z" } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized["schedule"].value("kind", std::string()) == "at");
	REQUIRE(normalized["schedule"].contains("atMs"));
	REQUIRE(normalized["schedule"]["atMs"].is_number_integer());
	REQUIRE(normalized["schedule"].value("atMs", static_cast<std::int64_t>(0)) > 0);
}

TEST_CASE("Cron normalize add infers name from payload when omitted", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "nightly sync for project alpha" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("name"));
	REQUIRE(normalized["name"].is_string());
	REQUIRE_FALSE(normalized.value("name", std::string()).empty());
	REQUIRE(normalized.value("name", std::string()).find("nightly sync") != std::string::npos);
}

TEST_CASE("Cron normalize add uses fallback inferred name when payload text is empty", "[cron][normalize]") {
	const CronJson params = {
		{ "schedule", { { "kind", "at" }, { "at", "2026-05-17T12:34:56Z" } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "" } } }
	};

	const CronJson normalized = CronNormalize::NormalizeAddInput(params);
	REQUIRE(normalized.contains("name"));
	REQUIRE(normalized["name"].is_string());
	REQUIRE(normalized.value("name", std::string()) == "cron-at");
}

TEST_CASE("Cron add response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-add-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"name\":\"job\",\"enabled\":true,\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"}}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.add", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-add-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"enabled\":true,\"schedule\":{},\"payload\":{}}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.add", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron add response validator enforces nested schedule shape", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-add-invalid-schedule-kind",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"name\":\"job\",\"enabled\":true,\"schedule\":{\"kind\":123,\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"}}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.add", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron update response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-update-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"name\":\"job\",\"enabled\":false,\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"}}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.update", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-update-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"name\":\"job\",\"enabled\":\"false\",\"schedule\":{},\"payload\":{}}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.update", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron update response validator enforces nested payload shape", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-update-invalid-payload-kind",
		.ok = true,
		.payloadJson = std::string(
			"{\"id\":\"cron-1\",\"name\":\"job\",\"enabled\":true,\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":123}}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.update", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron remove response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-remove-valid",
		.ok = true,
		.payloadJson = std::string("{\"ok\":true,\"removed\":true}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.remove", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-remove-invalid",
		.ok = true,
		.payloadJson = std::string("{\"ok\":true,\"removed\":\"yes\"}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.remove", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-run-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"queueDepth\":1,\"runState\":\"queued\"}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-run-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":\"1700000000000\",\"runState\":\"queued\"}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects queued reason with terminal runState", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-queued-reason-terminal-state",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":false,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"terminal\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects active state with non-queued reason", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-active-reason-mismatch",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":true,\"reason\":\"already_running\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"active\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects inconsistent queued semantics", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-consistency-queued-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"already_running\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"queued\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects enqueued terminal semantics", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-consistency-terminal-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"terminal\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Wake response validator enforces required fields", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "wake-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"mode\":\"now\",\"text\":\"wake\",\"requestedAtMs\":1700000000000}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("wake", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "wake-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"mode\":\"now\",\"text\":\"wake\",\"requestedAtMs\":\"1700000000000\"}"),
		.error = std::nullopt,
	};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("wake", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Wake response validator rejects unsupported mode taxonomy", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "wake-invalid-mode",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"mode\":\"later\",\"text\":\"wake\",\"requestedAtMs\":1700000000000}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("wake", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
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

TEST_CASE("Cron update validator rejects id containing path separators", "[cron][schema]") {
	const RequestFrame request{
		.id = "update-id-path-separator",
		.method = "cron.update",
		.paramsJson = std::string(
			"{\"id\":\"jobs/cron-1\",\"patch\":{\"enabled\":true}}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
	REQUIRE(issue.message.find("path separators") != std::string::npos);
}

TEST_CASE("Cron remove validator rejects jobId containing path separators", "[cron][schema]") {
	const RequestFrame request{
		.id = "remove-jobid-path-separator",
		.method = "cron.remove",
		.paramsJson = std::string("{\"jobId\":\"jobs\\\\cron-1\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.jobId") != std::string::npos);
	REQUIRE(issue.message.find("path separators") != std::string::npos);
}

TEST_CASE("Cron run validator rejects id containing path separators", "[cron][schema]") {
	const RequestFrame request{
		.id = "run-id-path-separator",
		.method = "cron.run",
		.paramsJson = std::string("{\"id\":\"jobs/cron-1\",\"mode\":\"force\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code == "schema_invalid_value");
	REQUIRE(issue.message.find("params.id") != std::string::npos);
	REQUIRE(issue.message.find("path separators") != std::string::npos);
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

TEST_CASE("Cron list response validator enforces nested job read-model field types", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-list-read-model-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"jobs\":[{\"id\":\"cron-1\",\"name\":\"nightly\",\"enabled\":true,\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"},\"createdAtMs\":1700000000000,\"updatedAtMs\":1700000001000}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.list", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-list-read-model-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"jobs\":[{\"id\":\"cron-1\",\"name\":\"nightly\",\"enabled\":true,\"schedule\":{\"kind\":123,\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"}}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.list", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron list response validator enforces non-empty job entry tokens", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-list-entry-missing-fields",
		.ok = true,
		.payloadJson = std::string(
			"{\"jobs\":[{\"id\":\"cron-1\",\"name\":\"nightly\",\"enabled\":true,\"payload\":{\"kind\":\"systemEvent\",\"text\":\"ping\"}}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
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

TEST_CASE("Cron runs response validator rejects inconsistent action to task-ledger phase semantics", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	SECTION("queued action requires queued phase") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-queued-action-active-phase",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"queued\",\"status\":\"queued\",\"taskLedgerPhase\":\"active\",\"taskLedgerTerminal\":false}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}

	SECTION("started action requires active phase") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-started-action-queued-phase",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"started\",\"status\":\"running\",\"taskLedgerPhase\":\"queued\",\"taskLedgerTerminal\":false}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}

	SECTION("finished action requires terminal phase") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-finished-action-active-phase",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"taskLedgerPhase\":\"active\",\"taskLedgerTerminal\":false}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}
}

TEST_CASE("Cron runs response validator rejects inconsistent action status semantics", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-runs-action-status-inconsistent",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"started\",\"status\":\"ok\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron runs response validator rejects inconsistent task-ledger terminal projection", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-runs-task-ledger-terminal-inconsistent",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"taskLedgerPhase\":\"terminal\",\"taskLedgerTerminal\":false}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron runs response validator accepts lifecycle-derived taskLedgerDisposition taxonomy", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-runs-disposition-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"failed\",\"taskLedgerDisposition\":\"failed\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
}

TEST_CASE("Cron runs response validator accepts expanded taskLedgerDisposition taxonomy values", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	SECTION("suppressed disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-suppressed",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"skipped\",\"taskLedgerDisposition\":\"suppressed\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("not_delivered disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-not-delivered",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"failed\",\"taskLedgerDisposition\":\"not_delivered\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("skipped disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-skipped",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"skipped\",\"taskLedgerDisposition\":\"skipped\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("unknown_job disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-unknown-job",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"skipped\",\"taskLedgerDisposition\":\"unknown_job\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("already_running disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-already-running",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"skipped\",\"taskLedgerDisposition\":\"already_running\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("not_due disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-not-due",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"skipped\",\"taskLedgerDisposition\":\"not_due\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("missing_terminal_run disposition") {
		const ResponseFrame validResponse{
			.id = "cron-runs-disposition-missing-terminal-run",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"failed\",\"taskLedgerDisposition\":\"missing_terminal_run\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("null optional transport enum fields") {
		const ResponseFrame validResponse{
			.id = "cron-runs-null-optional-enums",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"error\",\"deliveryStatus\":\"not-requested\","
				"\"failureDestinationMode\":null,\"failureAlertMode\":null,"
				"\"taskLedgerPhase\":\"terminal\",\"taskLedgerStatus\":\"error\","
				"\"taskLedgerDisposition\":\"scheduled\",\"taskLedgerTerminal\":true}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}
}

TEST_CASE(
	"Cron runs response validator enforces lifecycleState taxonomy and projection consistency",
	"[cron][schema][response][p7]") {
	SchemaValidationIssue issue{};

	SECTION("accepts finished lifecycle projection with aligned task-ledger fields") {
		const ResponseFrame validResponse{
			.id = "cron-runs-lifecycle-terminal-valid",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"ok\",\"lifecycleState\":\"terminal\","
				"\"taskLedgerPhase\":\"terminal\",\"taskLedgerDisposition\":\"scheduled\","
				"\"taskLedgerTerminal\":true,\"deliveryMode\":\"none\"}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
	}

	SECTION("rejects finished action with non-terminal lifecycleState") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-lifecycle-finished-non-terminal",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"ok\",\"lifecycleState\":\"queued\","
				"\"taskLedgerPhase\":\"terminal\",\"taskLedgerTerminal\":true}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}

	SECTION("rejects lifecycleState and taskLedgerPhase mismatch") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-lifecycle-phase-mismatch",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"ok\",\"lifecycleState\":\"terminal\","
				"\"taskLedgerPhase\":\"active\",\"taskLedgerTerminal\":true}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}

	SECTION("rejects unsupported lifecycleState taxonomy") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-lifecycle-taxonomy-invalid",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"ok\",\"lifecycleState\":\"done\","
				"\"taskLedgerPhase\":\"terminal\",\"taskLedgerTerminal\":true}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}

	SECTION("rejects unsupported deliveryMode taxonomy on run entries") {
		const ResponseFrame invalidResponse{
			.id = "cron-runs-delivery-mode-taxonomy-invalid",
			.ok = true,
			.payloadJson = std::string(
				"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\","
				"\"action\":\"finished\",\"status\":\"ok\",\"lifecycleState\":\"terminal\","
				"\"taskLedgerPhase\":\"terminal\",\"taskLedgerTerminal\":true,"
				"\"deliveryMode\":\"email\"}],"
				"\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
			.error = std::nullopt,
		};

		REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
		REQUIRE(issue.code == "schema_invalid_response");
	}
}

TEST_CASE("Cron runs response validator rejects unsupported value taxonomy", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-runs-taxonomy-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"deliveryStatus\":\"queued\",\"taskLedgerPhase\":\"terminal\",\"taskLedgerStatus\":\"ok\",\"taskLedgerDisposition\":\"scheduled\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects unsupported runState taxonomy", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-runstate-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"pending\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects active state without started", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-active-without-started",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"runState\":\"active\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects non-integer queuedAtMs", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-queued-atms-non-integer",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000.5,\"runState\":\"queued\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron run response validator rejects negative queueDepth", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-run-queue-depth-negative",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":true,\"started\":false,\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\",\"queuedAtMs\":1700000000000,\"queueDepth\":-1,\"runState\":\"queued\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.run", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron runs response validator enforces nested run read-model field types", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-runs-read-model-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"deliveryStatus\":\"delivered\",\"delivered\":true,\"deliveryHttpStatus\":200,\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-runs-read-model-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"delivered\":\"true\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron runs response validator enforces non-empty entry tokens", "[cron][schema][response]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidResponse{
		.id = "cron-runs-entry-missing-fields",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"action\":\"finished\",\"status\":\"ok\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
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

TEST_CASE("Cron timer suppresses webhook failure destination for scheme-case-equivalent target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-webhook-scheme-case-equivalent" },
			{ "name", "failure destination webhook scheme case equivalent" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "HTTPS://Example.Test/hooks/primary" },
					{ "simulateTransientFailure", true },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "https://example.test/hooks/primary" }
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
	REQUIRE(runs[0].value("failureDestinationError", std::string()) == "failure destination matches primary delivery target");
}

TEST_CASE("Cron timer suppresses webhook failure destination for default-port-equivalent target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-webhook-default-port-equivalent" },
			{ "name", "failure destination webhook default port equivalent" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://example.test:443/hooks/primary" },
					{ "simulateTransientFailure", true },
					{ "failureDestination",
						{
							{ "mode", "webhook" },
							{ "to", "https://example.test/hooks/primary" }
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

TEST_CASE("Cron timer marks announce failure destination unresolved when fallback target is unavailable", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-unresolved" },
			{ "name", "failure destination announce unresolved" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "bad-target" },
					{ "failureDestination",
						{
							{ "mode", "announce" }
						} }
				} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "announce");
	REQUIRE(
		runs[0].value("failureDestinationError", std::string()) ==
		"failure destination matches primary delivery target");
}

TEST_CASE("Cron timer suppresses webhook failure destination when runtime projects http target without delivery mode", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-http-target-without-mode") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-http-target-without-mode" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "main" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryTarget", "https://runtime.example/mode-inferred" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-http-target-without-mode" },
			{ "name", "runtime http target without delivery mode" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", {
				{ "mode", "announce" },
				{ "to", "room-1" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "https://runtime.example/mode-inferred" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(
		runs[0].value("failureDestinationError", std::string()) ==
		"failure destination matches primary delivery target");
}

TEST_CASE("Cron timer infers webhook failure destination mode from url alias when mode is omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-url-mode-inferred" },
			{ "name", "failure destination url mode inferred" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "bad-target" },
				{ "failureDestination", {
					{ "url", "https://example.test/failure" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://example.test/failure");
}

TEST_CASE("Cron timer infers runtime projected webhook modes from HTTP targets when runtime mode fields are omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-http-targets-mode-inferred") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-http-targets-mode-inferred" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "main" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryTarget", "https://runtime.example/primary" },
				{ "deliveryAttempted", true },
				{ "failureDestinationStatus", "not-delivered" },
				{ "failureDestinationTarget", "https://runtime.example/failure" },
				{ "failureDestinationAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-http-targets-mode-inferred" },
			{ "name", "runtime http targets mode inferred" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", {
				{ "mode", "announce" },
				{ "to", "room-1" },
				{ "failureDestination", {
					{ "mode", "announce" },
					{ "to", "ops-room" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationMode", std::string()) == "webhook");
}

TEST_CASE("Cron timer suppresses announce failure destination when runtime primary target falls back to session context", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-announce-session-target-suppression") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-primary-announce-session-fallback" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "session:abc" },
				{ "deliveryMode", "announce" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-announce-session-target-suppression" },
			{ "name", "runtime announce session target suppression" },
			{ "enabled", true },
			{ "sessionTarget", "session:abc" },
			{ "sessionKey", "abc" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "announce" },
				{ "channel", "last" },
				{ "failureDestination", {
					{ "mode", "announce" },
					{ "to", "session:abc" },
					{ "channel", "last" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(
		runs[0].value("failureDestinationError", std::string()) ==
		"failure destination matches primary delivery target");
}

TEST_CASE("Cron timer suppresses announce failure destination when runtime projects matching primary account id", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-announce-account-suppression") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-primary-announce-with-account" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "deliveryMode", "announce" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryTarget", "ops-room" },
				{ "deliveryChannel", "last" },
				{ "deliveryAccountId", "runtime-account" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-announce-account-suppression" },
			{ "name", "runtime announce account suppression" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "announce" },
				{ "to", "ops-room" },
				{ "accountId", "config-account" },
				{ "failureDestination", {
					{ "mode", "announce" },
					{ "to", "ops-room" },
					{ "channel", "last" },
					{ "accountId", "runtime-account" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryAccountId", std::string()) == "runtime-account");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(
		runs[0].value("failureDestinationError", std::string()) ==
		"failure destination matches primary delivery target");
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
	REQUIRE(runs[0].value("failureAlertStatus", std::string()) == "not-requested");
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
	REQUIRE(jobs[0]["state"]["lastFailureAlertMode"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertTarget"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertChannel"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertAccountId"].is_null());
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
	REQUIRE(jobs[0]["state"]["lastFailureAlertMode"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertTarget"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertChannel"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertAccountId"].is_null());
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
}

TEST_CASE("Cron timer failureAlert webhook mode falls back target to delivery.to", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-fallback-target" },
			{ "name", "alert webhook fallback target" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/delivery" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "webhook" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureAlertTarget", std::string()) == "https://alerts.example/delivery");
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(jobs[0]["state"].value("lastFailureAlertMode", std::string()) == "webhook");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example/delivery");
}

TEST_CASE("Cron timer failureAlert webhook mode falls back target to delivery url alias", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-fallback-url-alias" },
			{ "name", "alert webhook fallback url alias" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "url", "https://alerts.example/delivery-alias" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "webhook" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureAlertTarget", std::string()) == "https://alerts.example/delivery-alias");
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(jobs[0]["state"].value("lastFailureAlertMode", std::string()) == "webhook");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example/delivery-alias");
}

TEST_CASE("Cron timer failureAlert cooldown opens when alert route changes", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-route-change-cooldown" },
			{ "name", "alert route change cooldown" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-url" }, { "channel", "last" } } },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 600'000 }, { "mode", "announce" }, { "to", "team-secondary" }, { "channel", "alerts" } } },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "announce" },
					{ "lastFailureAlertTarget", "team-primary" },
					{ "lastFailureAlertChannel", "last" },
					{ "lastFailureAlertAccountId", nullptr }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE(runs[0].value("failureAlertMode", std::string()) == "announce");
	REQUIRE(runs[0].value("failureAlertTarget", std::string()) == "team-secondary");
	REQUIRE(runs[0].value("failureAlertChannel", std::string()) == "alerts");
	REQUIRE_FALSE(runs[0].value("failureAlertSuppressed", true));
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "team-secondary");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertChannel", std::string()) == "alerts");
}

TEST_CASE("Cron timer failureAlert cooldown remains active for webhook scheme-case equivalent route", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-equivalent-route" },
			{ "name", "alert webhook equivalent route" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert",
				{
					{ "after", 1 },
					{ "cooldownMs", 600'000 },
					{ "mode", "webhook" },
					{ "to", "HTTPS://alerts.example/route" }
				} },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/route" },
					{ "lastFailureAlertChannel", nullptr },
					{ "lastFailureAlertAccountId", nullptr }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "cooldown_active");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs - 1'000);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "HTTPS://alerts.example/route");
}

TEST_CASE("Cron timer failureAlert cooldown remains active for webhook host-case equivalent route", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-host-case-equivalent-route" },
			{ "name", "alert webhook host case equivalent route" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert",
				{
					{ "after", 1 },
					{ "cooldownMs", 600'000 },
					{ "mode", "webhook" },
					{ "to", "https://ALERTS.EXAMPLE/route" }
				} },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/route" },
					{ "lastFailureAlertChannel", nullptr },
					{ "lastFailureAlertAccountId", nullptr }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "cooldown_active");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs - 1'000);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://ALERTS.EXAMPLE/route");
}

TEST_CASE("Cron timer failureAlert cooldown remains active for webhook default-port equivalent route", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-default-port-equivalent-route" },
			{ "name", "alert webhook default port equivalent route" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert",
				{
					{ "after", 1 },
					{ "cooldownMs", 600'000 },
					{ "mode", "webhook" },
					{ "to", "https://alerts.example:443/route" }
				} },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/route" },
					{ "lastFailureAlertChannel", nullptr },
					{ "lastFailureAlertAccountId", nullptr }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "cooldown_active");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs - 1'000);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example:443/route");
}

TEST_CASE("Cron timer failureAlert webhook cooldown ignores announce-only account/channel snapshot drift", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-ignores-account-channel-drift" },
			{ "name", "alert webhook ignores account/channel drift" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert",
				{
					{ "after", 1 },
					{ "cooldownMs", 600'000 },
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/route" }
				} },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/route" },
					{ "lastFailureAlertChannel", "legacy-announce-channel" },
					{ "lastFailureAlertAccountId", "legacy-announce-account" }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "cooldown_active");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs - 1'000);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertMode", std::string()) == "webhook");
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example/route");
	REQUIRE(jobs[0]["state"]["lastFailureAlertChannel"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertAccountId"].is_null());
}

TEST_CASE("Cron timer failureAlert cooldown opens for materially changed webhook target", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-webhook-route-change" },
			{ "name", "alert webhook route change" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert",
				{
					{ "after", 1 },
					{ "cooldownMs", 600'000 },
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/route-v2" }
				} },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 1 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/route-v1" },
					{ "lastFailureAlertChannel", nullptr },
					{ "lastFailureAlertAccountId", nullptr }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureAlertTriggered", false));
	REQUIRE_FALSE(runs[0].value("failureAlertSuppressed", true));
	REQUIRE(jobs[0]["state"].value("lastFailureAlertAtMs", static_cast<std::int64_t>(0)) == nowMs);
	REQUIRE(jobs[0]["state"].value("lastFailureAlertTarget", std::string()) == "https://alerts.example/route-v2");
}

TEST_CASE("Cron timer clears stale failureAlert route snapshot when alert is disabled", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-disabled-clears-route-snapshot" },
			{ "name", "alert disabled clears route snapshot" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "https://alerts.example/primary" },
					{ "simulateTransientFailure", true }
				} },
			{ "failureAlert", false },
			{ "state",
				{
					{ "nextRunAtMs", nowMs - 1 },
					{ "consecutiveErrors", 3 },
					{ "lastFailureAlertAtMs", nowMs - 1'000 },
					{ "lastFailureAlertMode", "webhook" },
					{ "lastFailureAlertTarget", "https://alerts.example/stale" },
					{ "lastFailureAlertChannel", "stale" },
					{ "lastFailureAlertAccountId", "acct-stale" }
				} }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "disabled");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertMode"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertTarget"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertChannel"].is_null());
	REQUIRE(jobs[0]["state"]["lastFailureAlertAccountId"].is_null());
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

TEST_CASE("Cron timer announce failure destination falls back channel/account from primary delivery", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-failure-destination-announce-fallback-channel-account" },
			{ "name", "failure destination announce fallback channel account" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
			{ "delivery",
				{
					{ "mode", "webhook" },
					{ "to", "invalid-url" },
					{ "channel", "ops" },
					{ "accountId", "acc-primary" },
					{ "failureDestination",
						{
							{ "mode", "announce" }
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
	REQUIRE(runs[0].value("failureDestinationChannel", std::string()) == "ops");
	REQUIRE(runs[0].value("failureDestinationAccountId", std::string()) == "acc-primary");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationChannel", std::string()) == "ops");
	REQUIRE(jobs[0]["state"].value("lastFailureDestinationAccountId", std::string()) == "acc-primary");
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

TEST_CASE("Cron patch null sessionKey canonicalizes stale session target by payload kind", "[cron][normalize]") {
	SECTION("agentTurn payload falls back to isolated") {
		CronJson job = {
			{ "id", "cron-null-sessionkey-agentturn" },
			{ "name", "nightly" },
			{ "sessionKey", "agent:main:persisted" },
			{ "sessionTarget", "session:agent:main:persisted" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "run" } } },
			{ "state", CronJson::object() }
		};

		const CronJson patch = {
			{ "sessionKey", nullptr }
		};

		CronNormalize::ApplyPatch(job, patch);
		REQUIRE_FALSE(job.contains("sessionKey"));
		REQUIRE(job.value("sessionTarget", std::string()) == "isolated");
	}

	SECTION("systemEvent payload falls back to main") {
		CronJson job = {
			{ "id", "cron-null-sessionkey-systemevent" },
			{ "name", "nightly" },
			{ "sessionKey", "agent:main:persisted" },
			{ "sessionTarget", "session:agent:main:persisted" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ping" } } },
			{ "state", CronJson::object() }
		};

		const CronJson patch = {
			{ "sessionKey", nullptr }
		};

		CronNormalize::ApplyPatch(job, patch);
		REQUIRE_FALSE(job.contains("sessionKey"));
		REQUIRE(job.value("sessionTarget", std::string()) == "main");
	}
}

TEST_CASE("Cron runs response validator enforces failureAlertStatus taxonomy", "[cron][schema][response][step9]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-runs-failure-alert-status-valid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"error\",\"failureAlertStatus\":\"not-requested\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));

	const ResponseFrame invalidResponse{
		.id = "cron-runs-failure-alert-status-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"error\",\"failureAlertStatus\":\"suppressed\"}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", invalidResponse, issue));
	REQUIRE(issue.code == "schema_invalid_response");
}

TEST_CASE("Cron patch normalize infers webhook modes from url aliases when mode is omitted", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-patch-delivery-mode-infer" },
		{ "name", "patch delivery mode infer" },
		{ "delivery", {
			{ "mode", "announce" },
			{ "to", "room-1" },
			{ "failureDestination", {
				{ "mode", "announce" },
				{ "to", "ops-room" }
			} }
		} },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "ping" } } },
		{ "state", CronJson::object() }
	};

	const CronJson patch = {
		{ "delivery", {
			{ "url", "https://example.test/patch-primary" },
			{ "failureDestination", {
				{ "url", "https://example.test/patch-failure" }
			} }
		} }
	};

	CronNormalize::ApplyPatch(job, patch);
	REQUIRE(job.contains("delivery"));
	REQUIRE(job["delivery"].is_object());
	REQUIRE(job["delivery"].value("mode", std::string()) == "webhook");
	REQUIRE(job["delivery"].contains("failureDestination"));
	REQUIRE(job["delivery"]["failureDestination"].is_object());
	REQUIRE(
		job["delivery"]["failureDestination"].value("mode", std::string()) ==
		"webhook");
}

TEST_CASE("Cron normalize loaded job resolves sessionTarget current using persisted sessionKey", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-loaded-1" },
		{ "name", "loaded" },
		{ "sessionTarget", "current" },
		{ "sessionKey", "agent:main:loaded" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "run" } } }
	};

	CronNormalize::NormalizeLoadedJob(job);
	REQUIRE(job.value("sessionTarget", std::string()) == "session:agent:main:loaded");
}

TEST_CASE("Cron normalize loaded job resolves unknown sessionTarget by payload kind", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-loaded-2" },
		{ "name", "loaded" },
		{ "sessionTarget", "unexpected-target" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60000 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "run" } } }
	};

	CronNormalize::NormalizeLoadedJob(job);
	REQUIRE(job.value("sessionTarget", std::string()) == "isolated");
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
	REQUIRE(job.value("sessionTarget", std::string()) == "main");
}

TEST_CASE("Cron normalize patch backfills sessionKey from explicit session target", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-1" },
		{ "name", "job" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "hello" } } },
		{ "state", CronJson::object() }
	};

	const CronJson patch = {
		{ "sessionTarget", "session:agent:main:alpha" }
	};

	CronNormalize::ApplyPatch(job, patch);
	REQUIRE(job.value("sessionTarget", std::string()) == "session:agent:main:alpha");
	REQUIRE(job.value("sessionKey", std::string()) == "agent:main:alpha");
}

TEST_CASE("Cron normalize patch clears stale session target when sessionKey removed", "[cron][normalize]") {
	CronJson job = {
		{ "id", "cron-1" },
		{ "name", "job" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hello" } } },
		{ "sessionTarget", "session:agent:main:legacy" },
		{ "sessionKey", "agent:main:legacy" },
		{ "state", CronJson::object() }
	};

	const CronJson patch = {
		{ "sessionKey", "" }
	};

	CronNormalize::ApplyPatch(job, patch);
	REQUIRE_FALSE(job.contains("sessionKey"));
	REQUIRE(job.value("sessionTarget", std::string()) == "main");
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

TEST_CASE("Cron store loads envelope with legacy items/data array shapes", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() /
		"blazeclaw-cron-store-legacy-items-data-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "{\"version\":0,\"kind\":\"jobs\",\"items\":[{\"id\":\"job-items\",\"name\":\"legacy\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]}";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "{\"version\":0,\"kind\":\"runs\",\"data\":[]}";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().is_array());
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-items");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store loads envelope with legacy kind-matched array keys", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() /
		"blazeclaw-cron-store-legacy-kind-key-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "{\"version\":0,\"kind\":\"jobs\",\"jobs\":[{\"id\":\"job-kind-key\",\"name\":\"legacy\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]}";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "{\"version\":0,\"kind\":\"runs\",\"runs\":[]}";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().is_array());
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-kind-key");

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

TEST_CASE("Cron store reloads when backing file changes on disk", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-reload-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[{\"id\":\"job-a\",\"name\":\"A\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-a");

	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[{\"id\":\"job-b\",\"name\":\"B\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hello\"}}]";
	}

	store.EnsureLoaded();
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-b");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store falls back to backup when primary file is corrupted", "[cron][store]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-backup-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobsBackup(jobsPath.string() + ".bak", std::ios::binary | std::ios::trunc);
		jobsBackup << "[{\"id\":\"job-backup\",\"name\":\"backup\",\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},\"payload\":{\"kind\":\"systemEvent\",\"text\":\"hi\"}}]";
	}
	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "{";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-backup");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store prefers non-empty backup over empty primary and repairs primary store", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() /
		"blazeclaw-cron-store-backup-precedence-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << R"([])";
	}
	{
		std::ofstream jobsBackup(
			jobsPath.string() + ".bak",
			std::ios::binary | std::ios::trunc);
		jobsBackup << R"({"version":1,"jobs":[{"id":"job-from-backup","name":"backup","enabled":true,"schedule":{"kind":"every","everyMs":60000},"payload":{"kind":"systemEvent","text":"hi"}}]})";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << R"([])";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-from-backup");

	std::ifstream repairedJobs(jobsPath, std::ios::binary);
	REQUIRE(repairedJobs.is_open());
	const CronJson repairedParsed = ParseJsonStreamWithJson5Fallback(repairedJobs);
	REQUIRE(IsUsableJsonDocument(repairedParsed));
	REQUIRE(repairedParsed.contains("values"));
	REQUIRE(repairedParsed["values"].is_array());
	REQUIRE(repairedParsed["values"].size() == 1);
	REQUIRE(repairedParsed["values"][0].value("id", std::string()) == "job-from-backup");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store repairs runs file from backup when primary is corrupted", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() /
		"blazeclaw-cron-store-runs-backup-repair-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << R"([])";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "{";
	}
	{
		std::ofstream runsBackup(
			runsPath.string() + ".bak",
			std::ios::binary | std::ios::trunc);
		runsBackup << R"({"version":1,"kind":"runs","values":[{"jobId":"job-runs-backup","runId":"run-runs-backup","ts":123,"status":"ok","action":"finished"}]})";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Runs().size() == 1);
	REQUIRE(store.Runs()[0].value("runId", std::string()) == "run-runs-backup");

	std::ifstream repairedRuns(runsPath, std::ios::binary);
	REQUIRE(repairedRuns.is_open());
	const CronJson repairedParsed = ParseJsonStreamWithJson5Fallback(repairedRuns);
	REQUIRE(IsUsableJsonDocument(repairedParsed));
	REQUIRE(repairedParsed.contains("values"));
	REQUIRE(repairedParsed["values"].is_array());
	REQUIRE(repairedParsed["values"].size() == 1);
	REQUIRE(repairedParsed["values"][0].value("runId", std::string()) == "run-runs-backup");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store loads JSON5 jobs file with comments and trailing commas", "[cron][store][wp-e]") {
	const std::string json5Jobs = R"json5({
  /* legacy comment */
  "version": 1,
  "kind": "jobs",
  "values": [
    {
      "id": "job-json5",
      "name": "json5",
      "enabled": true,
      "schedule": { "kind": "every", "everyMs": 60000 },
      "payload": { "kind": "systemEvent", "text": "hi" },
    },
  ],
})json5";
	const CronJson parsed = ParseJsonWithJson5Fallback(json5Jobs);
	REQUIRE(IsUsableJsonDocument(parsed));
	REQUIRE(parsed["values"].is_array());
	REQUIRE(parsed["values"].size() == 1);

	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-json5-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << json5Jobs;
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().is_array());
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-json5");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store loads OpenClaw jobs envelope without kind/values keys", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-openclaw-jobs-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << R"({"version":1,"jobs":[{"id":"job-openclaw","name":"oc","enabled":true,"schedule":{"kind":"every","everyMs":60000},"payload":{"kind":"systemEvent","text":"hi"}}]})";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Jobs().size() == 1);
	REQUIRE(store.Jobs()[0].value("id", std::string()) == "job-openclaw");

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store merges per-job jsonl run logs with legacy aggregate", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-jsonl-merge-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root / "runs", ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[]";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << R"({"version":1,"kind":"runs","values":[{"jobId":"job-a","runId":"run-aggregate","ts":1000,"status":"ok","action":"finished"}]})";
	}
	{
		std::ofstream jsonl(root / "runs" / "job-a.jsonl", std::ios::binary | std::ios::trunc);
		jsonl << R"({"jobId":"job-a","runId":"run-jsonl","ts":2000,"status":"ok","action":"finished"})" << '\n';
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Runs().size() == 2);

	bool sawAggregate = false;
	bool sawJsonl = false;
	for (const auto& entry : store.Runs()) {
		const std::string runId = entry.value("runId", std::string());
		if (runId == "run-aggregate") {
			sawAggregate = true;
		}
		if (runId == "run-jsonl") {
			sawJsonl = true;
		}
	}
	REQUIRE(sawAggregate);
	REQUIRE(sawJsonl);

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store appends new runs to per-job jsonl on save", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-jsonl-append-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[]";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << "[]";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	store.Runs().push_back({
		{ "jobId", "job-append" },
		{ "runId", "run-new" },
		{ "ts", 42 },
		{ "status", "ok" },
		{ "action", "finished" }
	});
	store.SaveRuns();

	const std::filesystem::path jsonlPath =
		CronStoreService::ResolveRunLogPath(jobsPath, "job-append");
	REQUIRE(std::filesystem::exists(jsonlPath));

	std::ifstream jsonl(jsonlPath, std::ios::binary);
	REQUIRE(jsonl.is_open());
	std::string line;
	REQUIRE(std::getline(jsonl, line));
	REQUIRE(line.find("run-new") != std::string::npos);

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store migrates legacy aggregate runs into per-job jsonl files", "[cron][store][wp-e]") {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / "blazeclaw-cron-store-jsonl-migrate-test";
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::create_directories(root, ec);

	const std::filesystem::path jobsPath = root / "cron.jobs.json";
	const std::filesystem::path runsPath = root / "cron.runs.json";

	{
		std::ofstream jobs(jobsPath, std::ios::binary | std::ios::trunc);
		jobs << "[]";
	}
	{
		std::ofstream runs(runsPath, std::ios::binary | std::ios::trunc);
		runs << R"([{"jobId":"job-migrate","runId":"run-migrate","ts":99,"status":"ok","action":"finished"}])";
	}

	CronStoreService store(jobsPath, runsPath);
	store.EnsureLoaded();
	REQUIRE(store.Runs().size() == 1);
	store.SaveRuns();

	const std::filesystem::path jsonlPath =
		CronStoreService::ResolveRunLogPath(jobsPath, "job-migrate");
	REQUIRE(std::filesystem::exists(jsonlPath));

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("Cron store rejects unsafe per-job run log ids", "[cron][store][wp-e]") {
	const std::filesystem::path jobsPath =
		std::filesystem::temp_directory_path() / "cron.jobs.json";
	REQUIRE_FALSE(CronStoreService::IsSafeRunLogJobId(""));
	REQUIRE_FALSE(CronStoreService::IsSafeRunLogJobId("../escape"));
	REQUIRE_THROWS_AS(
		CronStoreService::ResolveRunLogPath(jobsPath, "../escape"),
		std::invalid_argument);
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

TEST_CASE("Cron timer computes cron expression with day-month-and-day-of-week fields", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobMonthDay = {
		{ "enabled", true },
		{ "schedule", { { "kind", "cron" }, { "expr", "15 10 1 1 *" } } },
		{ "state", CronJson::object() }
	};
	const auto nextMonthDay = timer.ComputeNextRunAtMs(jobMonthDay, nowMs);
	REQUIRE(nextMonthDay.has_value());
	REQUIRE(nextMonthDay.value() > nowMs);
	{
		const std::time_t t =
			static_cast<std::time_t>(nextMonthDay.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_min == 15);
		REQUIRE(tm.tm_hour == 10);
		REQUIRE(tm.tm_mday == 1);
		REQUIRE((tm.tm_mon + 1) == 1);
	}

	CronJson jobDayOfWeek = {
		{ "enabled", true },
		{ "schedule", { { "kind", "cron" }, { "expr", "0 9 * * 7" } } },
		{ "state", CronJson::object() }
	};
	const auto nextDayOfWeek = timer.ComputeNextRunAtMs(jobDayOfWeek, nowMs);
	REQUIRE(nextDayOfWeek.has_value());
	REQUIRE(nextDayOfWeek.value() > nowMs);
	{
		const std::time_t t =
			static_cast<std::time_t>(nextDayOfWeek.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_min == 0);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_wday == 0);
	}
}

TEST_CASE("Cron timer computes cron expression with range and stepped-range fields", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	SECTION("hour range with minute step") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "10-20/5 9-17 * * *" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_hour >= 9);
		REQUIRE(tm.tm_hour <= 17);
		REQUIRE(tm.tm_min >= 10);
		REQUIRE(tm.tm_min <= 20);
		REQUIRE(((tm.tm_min - 10) % 5) == 0);
	}

	SECTION("day-of-week wrapped range") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "0 9 * * 5-1" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_min == 0);
		const int wday = tm.tm_wday;
		REQUIRE((wday == 5 || wday == 6 || wday == 0 || wday == 1));
	}
}

TEST_CASE("Cron timer computes cron expression with named month and weekday aliases", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	SECTION("named month alias") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "0 9 1 jan *" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_min == 0);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_mday == 1);
		REQUIRE((tm.tm_mon + 1) == 1);
	}

	SECTION("named weekday alias range") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "0 9 * * mon-fri" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_min == 0);
		REQUIRE(tm.tm_wday >= 1);
		REQUIRE(tm.tm_wday <= 5);
	}
}

TEST_CASE("Cron timer computes cron expression with question-mark wildcard in day fields", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	SECTION("day-of-month question wildcard") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "0 9 ? * mon" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_min == 0);
		REQUIRE(tm.tm_wday == 1);
	}

	SECTION("day-of-week question wildcard") {
		CronJson job = {
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "0 9 1 jan ?" } } },
			{ "state", CronJson::object() }
		};

		const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);

		const std::time_t t =
			static_cast<std::time_t>(nextRun.value() / 1000);
		std::tm tm{};
		gmtime_s(&tm, &t);
		REQUIRE(tm.tm_hour == 9);
		REQUIRE(tm.tm_min == 0);
		REQUIRE(tm.tm_mday == 1);
		REQUIRE((tm.tm_mon + 1) == 1);
	}
}

TEST_CASE("Cron timer auto-disables cron job after repeated invalid timezone schedule errors", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-cron-invalid-timezone" },
			{ "name", "cron invalid timezone" },
			{ "agentId", "agent-cron" },
			{ "sessionKey", "agent:main:cron" },
			{ "enabled", true },
			{ "schedule",
				{
					{ "kind", "cron" },
					{ "expr", "* * * * *" },
					{ "tz", "Mars/Phobos" }
				} },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", CronJson::object() }
		}
	});

	for (int i = 0; i < 3; ++i) {
		timer.RecomputeSchedules(jobs, nowMs + (i * 1'000));
	}

	REQUIRE(jobs[0].value("enabled", true) == false);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].is_object());
	REQUIRE(jobs[0]["state"].value("scheduleErrorCount", 0) >= 3);
	REQUIRE(
		jobs[0]["state"].value("lastError", std::string()).find("schedule error") !=
		std::string::npos);
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabled", false));
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabledAtMs", 0LL) > 0);
	REQUIRE(
		jobs[0]["state"].value("scheduleAutoDisabledReason", std::string()) ==
		"schedule_error_threshold");
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableNotificationContextKey",
		std::string()) == "cron:job-cron-invalid-timezone:auto-disabled");
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableHeartbeatWakeRequested",
		false));
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableHeartbeatWakeRequestedAtMs",
		0LL) > 0);
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableHeartbeatWakeReason",
		std::string()) == "cron:job-cron-invalid-timezone:auto-disabled");
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableNotificationAgentId",
		std::string()) == "agent-cron");
	REQUIRE(jobs[0]["state"].value(
		"scheduleAutoDisableNotificationSessionKey",
		std::string()) == "agent:main:cron");
	REQUIRE(
		jobs[0]["state"].value(
			"scheduleAutoDisableNotificationText",
			std::string()).find("auto-disabled") != std::string::npos);
}

TEST_CASE("Cron timer accepts additional UTC/GMT zero-offset aliases", "[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	auto BuildJobWithTz = [](const std::string& tz) {
		return CronJson{
			{ "enabled", true },
			{ "schedule", { { "kind", "cron" }, { "expr", "* * * * *" }, { "tz", tz } } },
			{ "state", CronJson::object() }
		};
	};

	SECTION("UT") {
		const auto nextRun = timer.ComputeNextRunAtMs(BuildJobWithTz("UT"), nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);
	}

	SECTION("UTC0") {
		const auto nextRun = timer.ComputeNextRunAtMs(BuildJobWithTz("UTC0"), nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);
	}

	SECTION("Etc/UT") {
		const auto nextRun = timer.ComputeNextRunAtMs(BuildJobWithTz("Etc/UT"), nowMs);
		REQUIRE(nextRun.has_value());
		REQUIRE(nextRun.value() > nowMs);
	}
}

TEST_CASE(
	"Cron timer auto-disables cron job after repeated invalid cron expression field-count errors",
	"[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-cron-invalid-field-count" },
			{ "name", "cron invalid field count" },
			{ "enabled", true },
			{ "schedule",
				{
					{ "kind", "cron" },
					{ "expr", "* * *" }
				} },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", CronJson::object() }
		}
	});

	for (int i = 0; i < 3; ++i) {
		timer.RecomputeSchedules(jobs, nowMs + (i * 1'000));
	}

	REQUIRE(jobs[0].value("enabled", true) == false);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].is_object());
	REQUIRE(jobs[0]["state"].value("scheduleErrorCount", 0) >= 3);
	REQUIRE(
		jobs[0]["state"].value("lastError", std::string()).find("invalid cron expression field count") !=
		std::string::npos);
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabled", false));
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabledAtMs", 0LL) > 0);
	REQUIRE(
		jobs[0]["state"].value("scheduleAutoDisabledReason", std::string()) ==
		"schedule_error_threshold");
}

TEST_CASE(
	"Cron timer auto-disables cron job after repeated invalid cron token errors",
	"[cron][timer][step5]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-cron-invalid-token" },
			{ "name", "cron invalid token" },
			{ "enabled", true },
			{ "schedule",
				{
					{ "kind", "cron" },
					{ "expr", "61 * * * *" }
				} },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", CronJson::object() }
		}
	});

	for (int i = 0; i < 3; ++i) {
		timer.RecomputeSchedules(jobs, nowMs + (i * 1'000));
	}

	REQUIRE(jobs[0].value("enabled", true) == false);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].is_object());
	REQUIRE(jobs[0]["state"].value("scheduleErrorCount", 0) >= 3);
	REQUIRE(
		jobs[0]["state"].value("lastError", std::string()).find("invalid cron minute field") !=
		std::string::npos);
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabled", false));
	REQUIRE(jobs[0]["state"].value("scheduleAutoDisabledAtMs", 0LL) > 0);
	REQUIRE(
		jobs[0]["state"].value("scheduleAutoDisabledReason", std::string()) ==
		"schedule_error_threshold");
}

TEST_CASE(
	"Cron ops flushes schedule auto-disable notification hooks at threshold",
	"[cron][timer][wp-c]") {
	IsolatedCronOpsFixture fixture("schedule-auto-disable-notify");
	CronOpsService& ops = fixture.ops();
	std::vector<CronScheduleNotificationEvent> systemEvents;
	std::vector<CronScheduleNotificationEvent> heartbeatWakes;

	CronOpsService::ScheduleNotificationHooks hooks;
	hooks.enqueueSystemEvent = [&systemEvents](const CronScheduleNotificationEvent& event) {
		systemEvents.push_back(event);
	};
	hooks.requestHeartbeatNow = [&heartbeatWakes, &ops](
		const CronScheduleNotificationEvent& event) {
		heartbeatWakes.push_back(event);
		CronJson wakeParams = {
			{ "mode", kWakeModeNextHeartbeat }
		};
		if (!event.heartbeatWakeReason.empty()) {
			wakeParams["text"] = event.heartbeatWakeReason;
		}
		ops.EnqueueDeferredWakeRequest(wakeParams);
	};
	ops.SetScheduleNotificationHooks(std::move(hooks));

	const CronJson added = ops.Add({
		{ "name", "wp-c auto disable notify" },
		{ "agentId", "agent-wp-c" },
		{ "sessionKey", "agent:main:wp-c" },
		{ "enabled", true },
		{ "schedule",
			{
				{ "kind", "cron" },
				{ "expr", "* * * * *" },
				{ "tz", "Mars/Phobos" }
			} },
		{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } }
	});
	const std::string jobId = added.value("id", std::string());
	REQUIRE_FALSE(jobId.empty());

	for (int attempt = 0; attempt < 3; ++attempt) {
		(void)ops.Status(CronJson::object());
	}

	REQUIRE(systemEvents.size() == 1);
	REQUIRE(heartbeatWakes.size() == 1);
	REQUIRE(systemEvents[0].text.find("auto-disabled") != std::string::npos);
	REQUIRE(systemEvents[0].agentId == "agent-wp-c");
	REQUIRE(systemEvents[0].sessionKey == "agent:main:wp-c");
	REQUIRE(
		systemEvents[0].contextKey ==
		("cron:" + jobId + ":auto-disabled"));
	REQUIRE(
		heartbeatWakes[0].heartbeatWakeReason ==
		("cron:" + jobId + ":auto-disabled"));

	(void)ops.Status(CronJson::object());
	REQUIRE(systemEvents.size() == 1);
	REQUIRE(heartbeatWakes.size() == 1);
}

TEST_CASE(
	"Cron timer clears stuck runningAtMs during schedule recompute",
	"[cron][timer][wp-d]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-stuck-running" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state",
				{
					{ "runningAtMs", nowMs - kCronStuckRunMs - 1'000 },
					{ "nextRunAtMs", nowMs - 1'000 }
				} }
		}
	});

	const bool changed = timer.RecomputeSchedules(jobs, nowMs);
	REQUIRE(changed);
	REQUIRE(jobs[0]["state"]["runningAtMs"].is_null());
}

TEST_CASE(
	"Cron timer enforces minimum refire gap for cron-expression schedules",
	"[cron][timer][wp-d]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-min-refire" },
			{ "enabled", true },
			{ "schedule",
				{
					{ "kind", "cron" },
					{ "expr", "* * * * *" }
				} },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].contains("nextRunAtMs"));
	REQUIRE(jobs[0]["state"]["nextRunAtMs"].is_number_integer());
	REQUIRE(
		jobs[0]["state"]["nextRunAtMs"].get<std::int64_t>() >=
		nowMs + kCronMinRefireGapMs);
}

TEST_CASE(
	"Cron timer respects maxExecutionsPerPump batch limit",
	"[cron][timer][wp-d]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-batch-a" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "a" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		},
		{
			{ "id", "job-batch-b" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "b" } } },
			{ "state", { { "nextRunAtMs", nowMs - 2 } } }
		}
	});
	CronJson runs = CronJson::array();

	CronPumpOptions pumpOptions;
	pumpOptions.maxExecutionsPerPump = 1;
	const std::size_t executed = timer.PumpDueRuns(
		jobs,
		runs,
		nowMs,
		false,
		pumpOptions);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
}

TEST_CASE(
	"Cron timer replays missed cron slots from lastScheduledForMs with bounded replay limit",
	"[cron][timer][wp-d]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	const std::int64_t fiveMinutesMs = 5 * 60 * 1000;

	CronJson job = {
		{ "enabled", true },
		{ "schedule",
			{
				{ "kind", "cron" },
				{ "expr", "* * * * *" },
				{ "allowCronMissedRunByLastRun", true },
				{ "missedRunReplayLimit", 5 }
			} },
		{ "state", { { "lastScheduledForMs", nowMs - fiveMinutesMs } } }
	};

	const auto nextRun = timer.ComputeNextRunAtMs(job, nowMs);
	REQUIRE(nextRun.has_value());
	REQUIRE(nextRun.value() > (nowMs - fiveMinutesMs));
	REQUIRE(nextRun.value() <= nowMs);
}

TEST_CASE(
	"Cron ops emits realtime lifecycle events for add and finished runs",
	"[cron][timer][wp-d]") {
	IsolatedCronOpsFixture fixture("realtime-lifecycle-events");
	CronOpsService& ops = fixture.ops();
	std::vector<CronRealtimeEvent> events;

	CronOpsService::CronRealtimeEventHooks hooks;
	hooks.onEvent = [&events](const CronRealtimeEvent& event) {
		events.push_back(event);
	};
	ops.SetRealtimeEventHooks(std::move(hooks));

	const std::int64_t nowMs = blazeclaw::cron::UtcNowMs();
	const CronJson added = ops.Add({
		{ "name", "wp-d realtime" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } }
	});
	const std::string jobId = added.value("id", std::string());
	REQUIRE_FALSE(jobId.empty());
	(void)ops.Update({
		{ "id", jobId },
		{ "patch", { { "state", { { "nextRunAtMs", nowMs - 1 } } } } }
	});

	bool sawAdded = false;
	for (const CronRealtimeEvent& event : events) {
		if (event.action == "added" && event.jobId == jobId) {
			sawAdded = true;
		}
	}
	REQUIRE(sawAdded);

	events.clear();
	(void)ops.Wake({ { "mode", "now" }, { "text", "run" } });

	bool sawStarted = false;
	bool sawFinished = false;
	for (const CronRealtimeEvent& event : events) {
		if (event.jobId != jobId) {
			continue;
		}
		if (event.action == "started") {
			sawStarted = true;
			REQUIRE(event.runAtMs.has_value());
		}
		if (event.action == "finished") {
			sawFinished = true;
			REQUIRE_FALSE(event.status.empty());
		}
	}
	REQUIRE(sawStarted);
	REQUIRE(sawFinished);
}

TEST_CASE(
	"Cron gateway production wiring projects not-configured failure alert observability in cron.runs",
	"[cron][gateway][wp-f][step6][step10]") {
	GatewayCronProductionFixture fixture("production-failure-alert-not-configured");
	GatewayHost& host = fixture.gateway();

	const ResponseFrame cronAdd = RouteGatewayCron(
		host,
		"wpf-cron-add-alert-not-configured",
		"cron.add",
		std::string(
			"{\"name\":\"wp-f alert-not-configured job\",\"enabled\":true,"
			"\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},"
			"\"payload\":{\"kind\":\"systemEvent\",\"text\":\"gateway failure alert not configured\"},"
			"\"delivery\":{\"mode\":\"webhook\",\"to\":\"invalid-url\"}}"));
	REQUIRE(cronAdd.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.add", cronAdd));
	const std::string cronId =
		CronJson::parse(cronAdd.payloadJson.value()).value("id", std::string());
	REQUIRE_FALSE(cronId.empty());

	const ResponseFrame cronRun = RouteGatewayCron(
		host,
		"wpf-cron-run-alert-not-configured",
		"cron.run",
		std::string("{\"id\":\"") + cronId + "\",\"mode\":\"force\"}");
	REQUIRE(cronRun.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.run", cronRun));

	bool sawExpectedEntry = false;
	for (int attempt = 0; attempt < 3 && !sawExpectedEntry; ++attempt) {
		const ResponseFrame wakeNow = RouteGatewayCron(
			host,
			"wpf-wake-alert-not-configured-" + std::to_string(attempt),
			"wake",
			std::string("{\"mode\":\"now\",\"text\":\"execute failure alert not configured\"}"));
		REQUIRE(wakeNow.ok);
		REQUIRE(ValidateGatewayCronResponse("wake", wakeNow));

		const ResponseFrame cronRuns = RouteGatewayCron(
			host,
			"wpf-cron-runs-alert-not-configured-" + std::to_string(attempt),
			"cron.runs",
			std::string("{\"scope\":\"job\",\"id\":\"") + cronId +
			"\",\"limit\":20,\"offset\":0,\"sortDir\":\"desc\"}");
		REQUIRE(cronRuns.ok);
		REQUIRE(ValidateGatewayCronResponse("cron.runs", cronRuns));

		const CronJson runsPayload = CronJson::parse(cronRuns.payloadJson.value());
		REQUIRE(runsPayload.contains("entries"));
		REQUIRE(runsPayload["entries"].is_array());

		for (const auto& entry : runsPayload["entries"]) {
			if (!entry.is_object()) {
				continue;
			}
			if (entry.value("action", std::string()) != "finished") {
				continue;
			}

			sawExpectedEntry = true;
			REQUIRE(entry.value("status", std::string()) == "error");
			REQUIRE(entry.value("failureAlertSuppressed", false));
			REQUIRE(
				entry.value("failureAlertSuppressedReason", std::string()) ==
				"not_configured");
			REQUIRE(entry.value("failureAlertStatus", std::string()) == "not-requested");
			break;
		}
	}

	REQUIRE(sawExpectedEntry);
}

TEST_CASE(
	"Cron ops startup catchup defers excess missed jobs with staggered nextRunAtMs",
	"[cron][timer][wp-d]") {
	IsolatedCronOpsFixture fixture("startup-catchup-stagger");
	CronOpsService& ops = fixture.ops();
	CronSchedulerConfig config;
	config.maxMissedJobsPerRestart = 1;
	config.missedJobStaggerMs = 5'000;
	ops.SetSchedulerConfig(config);

	const std::int64_t nowMs = blazeclaw::cron::UtcNowMs();
	const CronJson immediate = ops.Add({
		{ "name", "catchup immediate" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "now" } } }
	});
	const CronJson deferredJob = ops.Add({
		{ "name", "catchup deferred" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "later" } } }
	});
	const std::string immediateId = immediate.value("id", std::string());
	const std::string deferredId = deferredJob.value("id", std::string());
	REQUIRE_FALSE(immediateId.empty());
	REQUIRE_FALSE(deferredId.empty());

	const CronJson immediatePatch = {
		{ "id", immediateId },
		{ "patch", { { "state", { { "nextRunAtMs", nowMs - 60'000 } } } } }
	};
	const CronJson deferredPatch = {
		{ "id", deferredId },
		{ "patch", { { "state", { { "nextRunAtMs", nowMs - 120'000 } } } } }
	};
	(void)ops.Update(immediatePatch);
	(void)ops.Update(deferredPatch);

	const CronJson list = ops.List(CronJson::object());
	REQUIRE(list.contains("jobs"));
	const CronJson& jobs = list["jobs"];
	REQUIRE(jobs.is_array());
	REQUIRE(jobs.size() == 2);

	std::int64_t deferredNextRunAtMs = 0;
	for (const auto& job : jobs) {
		if (job.value("id", std::string()) != deferredId) {
			continue;
		}
		REQUIRE(job.contains("state"));
		REQUIRE(job["state"].contains("nextRunAtMs"));
		REQUIRE(job["state"]["nextRunAtMs"].is_number_integer());
		deferredNextRunAtMs = job["state"]["nextRunAtMs"].get<std::int64_t>();
	}
	REQUIRE(deferredNextRunAtMs >= nowMs + config.missedJobStaggerMs);
}

TEST_CASE("Cron timer clears schedule auto-disable notification signaling after successful recompute", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-clear-auto-disable-signal" },
			{ "name", "clear auto disable signal" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } },
			{ "state",
				{
					{ "scheduleErrorCount", 3 },
					{ "scheduleAutoDisabled", true },
					{ "scheduleAutoDisabledAtMs", nowMs - 1000 },
					{ "scheduleAutoDisabledReason", "schedule_error_threshold" },
					{ "scheduleAutoDisableNotificationText", "stale" },
					{ "scheduleAutoDisableNotificationContextKey", "cron:stale:auto-disabled" },
					{ "scheduleAutoDisableNotificationAgentId", "agent-stale" },
					{ "scheduleAutoDisableNotificationSessionKey", "session:stale" },
					{ "scheduleAutoDisableHeartbeatWakeRequested", true },
					{ "scheduleAutoDisableHeartbeatWakeRequestedAtMs", nowMs - 500 },
					{ "scheduleAutoDisableHeartbeatWakeReason", "cron:stale:auto-disabled" }
				} }
		}
	});

	const bool changed = timer.RecomputeSchedules(jobs, nowMs);
	REQUIRE(changed);
	REQUIRE(jobs[0].contains("state"));
	REQUIRE(jobs[0]["state"].is_object());
	REQUIRE(jobs[0]["state"]["scheduleErrorCount"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisabled"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisabledAtMs"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisabledReason"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableNotificationText"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableNotificationContextKey"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableNotificationAgentId"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableNotificationSessionKey"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableHeartbeatWakeRequested"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableHeartbeatWakeRequestedAtMs"].is_null());
	REQUIRE(jobs[0]["state"]["scheduleAutoDisableHeartbeatWakeReason"].is_null());
}

TEST_CASE("Cron ops update schedule error resets stale auto-disable signaling fields", "[cron][ops][step6]") {
	IsolatedCronOpsFixture fixture("update-schedule-error-clears-stale-signals");
	CronOpsService& ops = fixture.ops();

	const CronJson added = ops.Add({
		{ "name", "ops stale signal reset" },
		{ "enabled", true },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "tick" } } }
	});
	const std::string id = added.value("id", std::string());
	REQUIRE_FALSE(id.empty());

	const std::int64_t nowMs = blazeclaw::cron::UtcNowMs();
	(void)ops.Update({
		{ "id", id },
		{ "patch", { { "state",
			{
				{ "scheduleAutoDisabled", true },
				{ "scheduleAutoDisabledAtMs", nowMs - 1000 },
				{ "scheduleAutoDisabledReason", "schedule_error_threshold" },
				{ "scheduleAutoDisableNotificationText", "stale" },
				{ "scheduleAutoDisableNotificationContextKey", "cron:stale:auto-disabled" },
				{ "scheduleAutoDisableNotificationAgentId", "agent-stale" },
				{ "scheduleAutoDisableNotificationSessionKey", "session:stale" },
				{ "scheduleAutoDisableHeartbeatWakeRequested", true },
				{ "scheduleAutoDisableHeartbeatWakeRequestedAtMs", nowMs - 500 },
				{ "scheduleAutoDisableHeartbeatWakeReason", "cron:stale:auto-disabled" }
			} } } }
	});

	const CronJson updated = ops.Update({
		{ "id", id },
		{ "patch", { { "schedule", { { "kind", "cron" }, { "expr", "* * * * *" }, { "tz", "invalid/tz" } } } } }
	});

	REQUIRE(updated.contains("state"));
	REQUIRE(updated["state"].is_object());
	REQUIRE(updated["state"].value("scheduleErrorCount", 0) >= 1);
	REQUIRE(updated["state"]["nextRunAtMs"].is_null());
	REQUIRE(updated["state"].value("lastError", std::string()).find("schedule error") != std::string::npos);
	REQUIRE(updated["state"]["scheduleAutoDisabled"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisabledAtMs"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisabledReason"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableNotificationText"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableNotificationContextKey"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableNotificationAgentId"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableNotificationSessionKey"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableHeartbeatWakeRequested"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableHeartbeatWakeRequestedAtMs"].is_null());
	REQUIRE(updated["state"]["scheduleAutoDisableHeartbeatWakeReason"].is_null());
}

TEST_CASE("Cron runs response validator accepts projection fields for jobs-service parity metadata", "[cron][schema][response][step9]") {
	SchemaValidationIssue issue{};

	const ResponseFrame validResponse{
		.id = "cron-runs-jobs-ts-projection-fields",
		.ok = true,
		.payloadJson = std::string(
			"{\"entries\":[{\"ts\":1700000000000,\"jobId\":\"cron-1\",\"runId\":\"manual:cron-1:1:1\",\"action\":\"finished\",\"status\":\"ok\",\"nextRunAtMs\":1700000060000,\"startedAtMs\":1700000000000,\"endedAtMs\":1700000000100,\"lifecycleState\":\"terminal\",\"taskLedgerStatus\":\"ok\",\"taskLedgerDisposition\":\"dispatched\",\"taskLedgerTerminal\":true}],\"total\":1,\"limit\":20,\"offset\":0,\"nextOffset\":null,\"hasMore\":false}"),
		.error = std::nullopt,
	};

	REQUIRE(GatewayProtocolSchemaValidator::ValidateResponseForMethod("cron.runs", validResponse, issue));
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

TEST_CASE("Cron timer resolves announce delivery target from session context when omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-announce-context" },
			{ "name", "announce context" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "notify" } } },
			{ "sessionTarget", "current" },
			{ "sessionKey", "agent:main:alpha" },
			{ "delivery", { { "mode", "announce" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "session:agent:main:alpha");
	REQUIRE(runs[0].value("sessionId", std::string()) == "session:agent:main:alpha");
}

TEST_CASE("Cron timer writes runtime usage telemetry for agentTurn execution", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-usage-agent-turn" },
			{ "name", "usage agent turn" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload",
				{
					{ "kind", "agentTurn" },
					{ "message", "Run analysis for weekly status report" },
					{ "model", "gpt-4.1" },
					{ "provider", "openai" }
				} },
			{ "sessionTarget", "isolated" },
			{ "delivery", { { "mode", "none" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].contains("usage"));
	REQUIRE(runs[0]["usage"].is_object());
	REQUIRE(runs[0]["usage"].contains("promptTokens"));
	REQUIRE(runs[0]["usage"].contains("completionTokens"));
	REQUIRE(runs[0]["usage"].contains("totalTokens"));
	REQUIRE(runs[0]["usage"].value("totalTokens", 0) >= runs[0]["usage"].value("promptTokens", 0));
	REQUIRE(runs[0].value("model", std::string()) == "gpt-4.1");
	REQUIRE(runs[0].value("provider", std::string()) == "openai");
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

TEST_CASE("Cron timer suppresses failure alert while retry is pending", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-alert-retry-pending" },
			{ "name", "alert retry pending" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "ok" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "https://alerts.example/fail" }, { "simulateHttpStatus", 503 } } },
			{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 30'000 }) } } },
			{ "failureAlert", { { "after", 1 }, { "cooldownMs", 10'000 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "consecutiveErrors", 0 }, { "retryAttempt", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("retryScheduled", false));
	REQUIRE_FALSE(runs[0].value("failureAlertTriggered", true));
	REQUIRE(runs[0].value("failureAlertSuppressed", false));
	REQUIRE(runs[0].value("failureAlertSuppressedReason", std::string()) == "retry_pending");
	REQUIRE(jobs[0]["state"].value("failureAlertSuppressedReason", std::string()) == "retry_pending");
	REQUIRE(jobs[0]["state"]["lastFailureAlertAtMs"].is_null());
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

TEST_CASE("Cron runs validator accepts manual lifecycle status filter in single status field", "[cron][schema]") {
	const RequestFrame request{
		.id = "runs-status-manual-lifecycle-single",
		.method = "cron.runs",
		.paramsJson = std::string("{\"status\":\"running\"}")
	};

	SchemaValidationIssue issue{};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(request, issue));
	REQUIRE(issue.code.empty());
}

TEST_CASE("Cron ops manual terminal hook carries retry and failure-alert metadata", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("manual-terminal-metadata");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "manual terminal metadata carry-forward" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", {
			{ "mode", "webhook" },
			{ "to", "https://example.test/hook" },
			{ "simulateHttpStatus", 503 }
		} },
		{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 5'000 }) } } },
		{ "failureAlert", {
			{ "after", 1 },
			{ "cooldownMs", 0 },
			{ "mode", "announce" },
			{ "to", "ops-room" }
		} },
		{ "deleteAfterRun", true }
	});
	const std::string jobId = added.value("id", std::string());
	REQUIRE_FALSE(jobId.empty());

	CronJson run = ops.Run({ { "id", jobId }, { "mode", "force" } });
	REQUIRE(run.value("enqueued", false));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	const CronJson& payload = failedPayloads.back();
	REQUIRE(payload.contains("retryAttempt"));
	REQUIRE(payload.contains("retryScheduled"));
	REQUIRE(payload.contains("retryScheduledAtMs"));
	REQUIRE(payload.contains("nextRunAtMs"));
	REQUIRE(payload.contains("failureAlertTriggered"));
	REQUIRE(payload.contains("failureAlertSuppressed"));
	REQUIRE(payload.contains("failureAlertMode"));
	REQUIRE(payload.contains("failureAlertTarget"));
	REQUIRE(payload.contains("deliveryMode"));
	REQUIRE(payload.contains("deliveryTarget"));
	REQUIRE(payload.contains("deliveryAttempted"));
	REQUIRE(payload.contains("deliveryHttpStatus"));
	REQUIRE(payload.contains("deliveryError"));
	REQUIRE(payload.contains("deliveryChannel"));
	REQUIRE(payload.contains("deliveryAccountId"));
	REQUIRE(payload.contains("failureDestinationStatus"));
	REQUIRE(payload.contains("failureDestinationMode"));
	REQUIRE(payload.contains("failureDestinationAttempted"));
	REQUIRE(payload.contains("failureDestinationHttpStatus"));
	REQUIRE(payload.contains("failureDestinationChannel"));
	REQUIRE(payload.contains("failureDestinationAccountId"));
	REQUIRE(payload.contains("failureDestinationError"));
	REQUIRE(payload.contains("heartbeatBusyAttempts"));
	REQUIRE(payload.contains("heartbeatFallbackWakeRequested"));
	REQUIRE(payload.contains("heartbeatFallbackWakeRequestedAtMs"));
}

TEST_CASE(
	"Cron timer projects runtime execution provenance into state and run logs",
	"[cron][timer][p0][p4]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-provenance") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-provenance" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-provenance" },
			{ "name", "runtime provenance" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "runtime lane" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(jobs.size() == 1);

	REQUIRE(runs[0].value("runtimeExecutionPath", std::string()) == "runtime");
	REQUIRE(runs[0].value("runtimeAdapterRegistered", false));
	REQUIRE(runs[0].value("runtimeAdapterInvoked", false));
	REQUIRE(runs[0].value("runtimeHandled", false));
	REQUIRE_FALSE(runs[0].value("simulationFallbackUsed", true));

	REQUIRE(jobs[0]["state"].value("lastRuntimeExecutionPath", std::string()) == "runtime");
	REQUIRE(jobs[0]["state"].value("lastRuntimeAdapterRegistered", false));
	REQUIRE(jobs[0]["state"].value("lastRuntimeAdapterInvoked", false));
	REQUIRE(jobs[0]["state"].value("lastRuntimeHandled", false));
	REQUIRE_FALSE(jobs[0]["state"].value("lastSimulationFallbackUsed", true));
}

TEST_CASE(
	"Cron ops terminal hook projects runtime execution provenance metadata",
	"[cron][ops][p0][p4]") {
	IsolatedCronOpsFixture fixture("runtime-provenance-terminal-hook");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> completedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId =
		[&completedPayloads](const CronJson& payload) {
			completedPayloads.push_back(payload);
		};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "runtime provenance hook" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "runtime provenance hook" } } },
		{ "deleteAfterRun", true }
	});
	const std::string jobId = added.value("id", std::string());
	REQUIRE_FALSE(jobId.empty());

	CronJson run = ops.Run({ { "id", jobId }, { "mode", "force" } });
	REQUIRE(run.value("enqueued", false));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "runtime provenance" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(completedPayloads.empty());
	const CronJson& payload = completedPayloads.back();
	REQUIRE(payload.value("runtimeExecutionPath", std::string()) == "simulation");
	REQUIRE(payload.value("runtimeAdapterRegistered", true) == false);
	REQUIRE(payload.value("runtimeAdapterInvoked", true) == false);
	REQUIRE(payload.value("runtimeHandled", true) == false);
	REQUIRE(payload.value("simulationFallbackUsed", false));
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
			"{\"scope\":\"all\",\"statuses\":[\"ok\",\"error\"],\"deliveryStatuses\":[\"delivered\",\"not-delivered\"],\"limit\":20}")
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
			{ "sessionTarget", "isolated" },
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

TEST_CASE("Cron timer runtime adapters override simulation outcome for systemEvent", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t adapterNowMs)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-main") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "ok" },
				{ "summary", "runtime-main-success" },
				{ "sessionId", "main" },
				{ "sessionKey", "runtime-main" },
				{ "model", "gpt-runtime" },
				{ "provider", "azure-openai" },
				{ "usage", {
					{ "promptTokens", 21 },
					{ "completionTokens", 5 },
					{ "totalTokens", 26 }
				} },
				{ "observedAtMs", adapterNowMs }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-main" },
			{ "name", "runtime adapter main" },
			{ "enabled", true },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-success");
	REQUIRE(runs[0].value("sessionKey", std::string()) == "runtime-main");
	REQUIRE(runs[0].value("model", std::string()) == "gpt-runtime");
	REQUIRE(runs[0].value("provider", std::string()) == "azure-openai");
	REQUIRE(runs[0]["usage"].value("promptTokens", 0) == 21);
	REQUIRE(runs[0]["usage"].value("completionTokens", 0) == 5);
	REQUIRE(runs[0]["usage"].value("totalTokens", 0) == 26);
	REQUIRE(jobs[0]["state"].value("lastModel", std::string()) == "gpt-runtime");
	REQUIRE(jobs[0]["state"].value("lastProvider", std::string()) == "azure-openai");
}

TEST_CASE("Cron timer infers payload kind for runtime adapter routing when payload kind is omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-main-kind-inferred") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-main-kind-inferred" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-main-kind-inferred" },
			{ "name", "runtime adapter main kind inferred" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "text", "wake" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-kind-inferred");
}

TEST_CASE("Cron timer runtime adapters override simulation outcome for agentTurn", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t adapterNowMs)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-isolated") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "error" },
				{ "summary", "runtime-isolated-timeout" },
				{ "error", "runtime timeout" },
				{ "errorCategory", "timeout" },
				{ "retryable", true },
				{ "timedOut", true },
				{ "sessionId", "isolated" },
				{ "sessionKey", "runtime-iso" },
				{ "usage", {
					{ "promptTokens", 11 },
					{ "completionTokens", 0 },
					{ "totalTokens", 11 }
				} },
				{ "observedAtMs", adapterNowMs }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-isolated" },
			{ "name", "runtime adapter isolated" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-isolated-timeout");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "timeout");
	REQUIRE(runs[0].value("timedOut", false));
	REQUIRE(runs[0].value("sessionKey", std::string()) == "runtime-iso");
	REQUIRE(runs[0]["usage"].value("promptTokens", 0) == 11);
	REQUIRE(runs[0]["usage"].value("completionTokens", 0) == 0);
	REQUIRE(runs[0]["usage"].value("totalTokens", 0) == 11);
	REQUIRE(jobs[0]["state"].value("lastRunTimedOut", false));
}

TEST_CASE("Cron timer runtime adapter infers timeout category from timedOut flag", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t adapterNowMs)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-timeout-infer") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "error" },
				{ "summary", "runtime timeout inferred" },
				{ "error", "runtime timed out" },
				{ "timedOut", true },
				{ "sessionId", "isolated" },
				{ "observedAtMs", adapterNowMs }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-timeout-infer" },
			{ "name", "runtime adapter timeout infer" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("timedOut", false));
	REQUIRE(runs[0].value("errorCategory", std::string()) == "timeout");
}

TEST_CASE("Cron timer runtime adapter handled=true bypasses agentTurn timeout simulation", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-handled-timeout") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-handled-ok" },
				{ "sessionId", "isolated" },
				{ "sessionKey", "runtime-handled" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-handled-timeout" },
			{ "name", "runtime adapter handled timeout" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" }, { "timeoutSeconds", 1 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-handled-ok");
	REQUIRE(runs[0].value("sessionKey", std::string()) == "runtime-handled");
}

TEST_CASE("Cron timer runtime adapter handled=true allows systemEvent without text", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-handled-empty-text") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-main-handled" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-handled-empty-text" },
			{ "name", "runtime adapter handled empty text" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-handled");
}

TEST_CASE("Cron timer infers runtime handled for systemEvent outcome without handled flag", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-inferred-handled-main") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "ok" },
				{ "summary", "runtime-main-inferred-handled" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-inferred-handled-main" },
			{ "name", "runtime inferred handled main" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-inferred-handled");
}

TEST_CASE("Cron timer infers runtime handled for agentTurn outcome without handled flag", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-inferred-handled-isolated") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "ok" },
				{ "summary", "runtime-isolated-inferred-handled" },
				{ "sessionId", "isolated" },
				{ "sessionKey", "runtime-inferred" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-inferred-handled-isolated" },
			{ "name", "runtime inferred handled isolated" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-isolated-inferred-handled");
	REQUIRE(runs[0].value("sessionKey", std::string()) == "runtime-inferred");
}

TEST_CASE(
	"Cron timer coerces main-target agentTurn payload into runtime main-session lane",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-main-coerce-agentturn") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-main-coerced" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-main-coerce-agentturn" },
			{ "name", "runtime main coerce" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "bridge to main" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-coerced");
	REQUIRE(runs[0].value("runtimeExecutionPath", std::string()) == "runtime");
	REQUIRE_FALSE(runs[0].value("simulationFallbackUsed", false));
}

TEST_CASE(
	"Cron timer coerces isolated-target systemEvent payload into runtime isolated lane",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-isolated-coerce-systemevent") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-isolated-coerced" },
				{ "sessionId", "isolated" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-isolated-coerce-systemevent" },
			{ "name", "runtime isolated coerce" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "bridge to isolated" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-isolated-coerced");
	REQUIRE(runs[0].value("runtimeExecutionPath", std::string()) == "runtime");
	REQUIRE_FALSE(runs[0].value("simulationFallbackUsed", false));
}

TEST_CASE("Cron timer explicit handled=false keeps systemEvent simulation fallback", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-handled-false-main") {
				return std::nullopt;
			}
			return CronJson{
				{ "handled", false },
				{ "status", "ok" },
				{ "summary", "runtime-main-explicit-unhandled" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-handled-false-main" },
			{ "name", "runtime explicit handled false main" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "fallback text" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-explicit-unhandled");
	REQUIRE(runs[0].value("sessionId", std::string()) == "main");
	REQUIRE(runs[0].contains("usage"));
	REQUIRE(runs[0]["usage"].is_object());
	REQUIRE(runs[0]["usage"].value("promptTokens", 0) >= 1);
}

TEST_CASE("Cron timer explicit handled=false keeps agentTurn timeout fallback", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-handled-false-agent") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", false },
				{ "status", "ok" },
				{ "summary", "runtime-agent-explicit-unhandled" },
				{ "sessionId", "isolated" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-handled-false-agent" },
			{ "name", "runtime explicit handled false agent" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" }, { "timeoutSeconds", 1 } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "timeout");
	REQUIRE(runs[0].value("timedOut", false));
}

TEST_CASE(
	"Cron timer preferRuntimeExecution rejects missing adapter result without simulation",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-production-missing") {
				return std::nullopt;
			}
			return std::nullopt;
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-production-missing" },
			{ "name", "production runtime missing" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "runtime_unavailable");
	REQUIRE_FALSE(runs[0].contains("usage"));
}

TEST_CASE(
	"Cron timer preferRuntimeExecution rejects unregistered adapter lane without simulation",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	// Intentionally keep adapters.mainSession unset to validate unregistered lane behavior.
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-production-unregistered-main" },
			{ "name", "production runtime unregistered main" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "runtime required" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "runtime_unavailable");
	REQUIRE_FALSE(runs[0].value("simulationFallbackUsed", false));
	REQUIRE_FALSE(runs[0].contains("usage"));
}

TEST_CASE(
	"Cron timer preferRuntimeExecution keeps explicit handled=false simulation fallback",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-production-handled-false") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", false },
				{ "status", "ok" },
				{ "summary", "explicit-unhandled-production" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-production-handled-false" },
			{ "name", "production explicit handled false" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "fallback text" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("summary", std::string()) == "explicit-unhandled-production");
	REQUIRE(runs[0].contains("usage"));
	REQUIRE(runs[0]["usage"].value("promptTokens", 0) >= 1);
}

TEST_CASE(
	"Cron timer preferRuntimeExecution uses runtime usage not synthetic tokens",
	"[cron][timer][wp-a]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.preferRuntimeExecution = true;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t adapterNowMs)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-production-usage") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "production-runtime-usage" },
				{ "sessionId", "isolated" },
				{ "usage", {
					{ "promptTokens", 99 },
					{ "completionTokens", 7 },
					{ "totalTokens", 106 }
				} },
				{ "observedAtMs", adapterNowMs }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-production-usage" },
			{ "name", "production runtime usage" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "this message would synthesize many tokens if simulated" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("summary", std::string()) == "production-runtime-usage");
	REQUIRE(runs[0]["usage"].value("promptTokens", 0) == 99);
	REQUIRE(runs[0]["usage"].value("completionTokens", 0) == 7);
	REQUIRE(runs[0]["usage"].value("totalTokens", 0) == 106);
}

TEST_CASE("Cron timer schedules bounded retry when main heartbeat adapter reports busy", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-heartbeat-busy") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "busy", true },
				{ "status", "error" },
				{ "summary", "runtime-main-busy" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-heartbeat-busy" },
			{ "name", "runtime heartbeat busy" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" }, { "heartbeatBusyMaxAttempts", 3 }, { "heartbeatBusyDelayMs", 2000 } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "https://example.test/primary" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "https://example.test/failure" }
				} }
			} },
			{ "retry", { { "maxAttempts", 3 }, { "backoffMs", CronJson::array({ 9'999 }) } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "heartbeatBusyAttempts", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "heartbeat_busy");
	REQUIRE(runs[0].value("retryScheduled", false));
	REQUIRE(runs[0].value("retryAttempt", 0) == 1);
	REQUIRE(runs[0].value("retryScheduledAtMs", static_cast<std::int64_t>(0)) == nowMs + 2000);
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-requested");
	REQUIRE_FALSE(runs[0].value("deliveryAttempted", true));
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-requested");
	REQUIRE_FALSE(runs[0].value("failureDestinationAttempted", true));
	REQUIRE(jobs[0]["state"].value("heartbeatBusyAttempts", 0) == 1);
	REQUIRE_FALSE(jobs[0]["state"].value("heartbeatFallbackWakeRequested", true));
}

TEST_CASE("Cron timer requests fallback wake after bounded main heartbeat busy retries", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-heartbeat-busy-fallback") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "busy", true },
				{ "status", "error" },
				{ "summary", "runtime-main-busy-fallback" },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-heartbeat-busy-fallback" },
			{ "name", "runtime heartbeat busy fallback" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" }, { "heartbeatBusyMaxAttempts", 1 }, { "heartbeatBusyDelayMs", 1500 } } },
			{ "delivery", {
				{ "mode", "announce" },
				{ "to", "ops-room" },
				{ "failureDestination", {
					{ "mode", "announce" },
					{ "to", "ops-fallback" }
				} }
			} },
			{ "retry", { { "maxAttempts", 3 }, { "backoffMs", CronJson::array({ 9'999 }) } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "heartbeatBusyAttempts", 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "heartbeat_busy_fallback");
	REQUIRE_FALSE(runs[0].value("retryScheduled", true));
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-requested");
	REQUIRE_FALSE(runs[0].value("deliveryAttempted", true));
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "not-requested");
	REQUIRE_FALSE(runs[0].value("failureDestinationAttempted", true));
	REQUIRE(jobs[0]["state"].value("heartbeatBusyAttempts", -1) == 0);
	REQUIRE(jobs[0]["state"].value("heartbeatFallbackWakeRequested", false));
	REQUIRE(jobs[0]["state"].value("heartbeatFallbackWakeRequestedAtMs", static_cast<std::int64_t>(0)) == nowMs);
}

TEST_CASE("Cron timer heartbeat-busy lane honors runtime retryAfter override", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-heartbeat-busy-retryafter") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "busy", true },
				{ "status", "error" },
				{ "summary", "runtime-main-busy-retry-after" },
				{ "retryAfterMs", 3500 },
				{ "sessionId", "main" }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-heartbeat-busy-retryafter" },
			{ "name", "runtime heartbeat busy retryAfter" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" }, { "heartbeatBusyMaxAttempts", 3 }, { "heartbeatBusyDelayMs", 2000 } } },
			{ "retry", { { "maxAttempts", 3 }, { "backoffMs", CronJson::array({ 9'999 }) } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 }, { "heartbeatBusyAttempts", 0 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "heartbeat_busy");
	REQUIRE(runs[0].value("retryScheduled", false));
	REQUIRE(runs[0].value("retryScheduledAtMs", static_cast<std::int64_t>(0)) == nowMs + 3500);
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-requested");
	REQUIRE_FALSE(runs[0].value("deliveryAttempted", true));
	REQUIRE(jobs[0]["state"].value("heartbeatBusyAttempts", 0) == 1);
}

TEST_CASE("Cron timer runtime adapter can skip delivery simulation with runtime transport outcome", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-skip-delivery") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "skipDelivery", true },
				{ "status", "ok" },
				{ "summary", "runtime-main-delivery-handled" },
				{ "sessionId", "main" },
				{ "deliveryStatus", "delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/hook" },
				{ "deliveryAttempted", true },
				{ "deliveryHttpStatus", 202 },
				{ "delivered", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-skip-delivery" },
			{ "name", "runtime adapter skip delivery" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://runtime.example/hook");
	REQUIRE(runs[0].value("deliveryAttempted", false));
	REQUIRE(runs[0].value("deliveryHttpStatus", 0) == 202);
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-delivery-handled");
}

TEST_CASE("Cron timer infers delivery-simulation bypass when runtime projects transport outcome", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-infer-skip-delivery") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-main-transport-projected" },
				{ "sessionId", "main" },
				{ "deliveryStatus", "delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/inferred" },
				{ "deliveryAttempted", true },
				{ "deliveryHttpStatus", 204 },
				{ "delivered", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-infer-skip-delivery" },
			{ "name", "runtime adapter inferred skip delivery" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://runtime.example/inferred");
	REQUIRE(runs[0].value("deliveryHttpStatus", 0) == 204);
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-main-transport-projected");
}

TEST_CASE("Cron timer infers failure-destination bypass when runtime projects terminal transport outcome", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-infer-failure-destination") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-isolated-delivery-failed" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/primary" },
				{ "deliveryAttempted", true },
				{ "failureDestinationStatus", "delivered" },
				{ "failureDestinationMode", "webhook" },
				{ "failureDestinationTarget", "https://runtime.example/failure" },
				{ "failureDestinationAttempted", true },
				{ "failureDestinationHttpStatus", 200 }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-infer-failure-destination" },
			{ "name", "runtime adapter inferred failure destination" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "invalid-target" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "invalid-target-failure" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://runtime.example/failure");
	REQUIRE(runs[0].value("failureDestinationHttpStatus", 0) == 200);
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-isolated-delivery-failed");
}

TEST_CASE("Cron timer infers runtime projected delivery status unknown when transport fields omit deliveryStatus", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-project-delivery-unknown") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-primary-transport-without-status" },
				{ "sessionId", "main" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/unknown" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-project-delivery-unknown" },
			{ "name", "runtime projected delivery unknown" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "unknown");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://runtime.example/unknown");
	REQUIRE_FALSE(runs[0].value("delivered", true));
}

TEST_CASE("Cron timer infers runtime projected failure destination status unknown when terminal fields omit failureDestinationStatus", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-project-failure-unknown") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-failure-transport-without-status" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/primary" },
				{ "deliveryAttempted", true },
				{ "failureDestinationMode", "webhook" },
				{ "failureDestinationTarget", "https://runtime.example/failure-unknown" },
				{ "failureDestinationAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-project-failure-unknown" },
			{ "name", "runtime projected failure unknown" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "invalid-target" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "invalid-target-failure" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "unknown");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://runtime.example/failure-unknown");
	REQUIRE(runs[0].value("summary", std::string()) == "runtime-failure-transport-without-status");
}

TEST_CASE("Cron timer infers runtime projected delivery status from delivered marker when deliveryStatus is omitted", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-project-delivered-marker") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "runtime-delivered-marker" },
				{ "sessionId", "main" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/delivered-marker" },
				{ "deliveryAttempted", true },
				{ "delivered", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-project-delivered-marker" },
			{ "name", "runtime projected delivered marker" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "webhook" }, { "to", "invalid-target" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "ok");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://runtime.example/delivered-marker");
	REQUIRE(runs[0].value("delivered", false));
}

TEST_CASE("Cron timer preserves failure-destination simulation when runtime projects primary transport only", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-partial-primary-only") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-primary-only-projected" },
				{ "error", "runtime primary delivery failed" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/primary-only" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-partial-primary-only" },
			{ "name", "runtime partial primary only" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "https://primary.example/fail" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "https://failure.example/fallback" },
					{ "simulateHttpStatus", 201 }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://runtime.example/primary-only");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationHttpStatus", 0) == 201);
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://failure.example/fallback");
}

TEST_CASE("Cron timer preserves primary delivery simulation when runtime projects failure destination only", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-partial-failure-only") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-failure-only-projected" },
				{ "error", "runtime primary failure projected" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "failureDestinationStatus", "delivered" },
				{ "failureDestinationMode", "webhook" },
				{ "failureDestinationTarget", "https://runtime.example/failure-only" },
				{ "failureDestinationAttempted", true },
				{ "failureDestinationHttpStatus", 202 }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-partial-failure-only" },
			{ "name", "runtime partial failure only" },
			{ "enabled", true },
			{ "sessionTarget", "isolated" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "to", "https://primary.example/ok" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "https://failure.example/unused" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("deliveryTarget", std::string()) == "https://primary.example/ok");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "delivered");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://runtime.example/failure-only");
	REQUIRE(runs[0].value("failureDestinationHttpStatus", 0) == 202);
}

TEST_CASE("Cron timer suppresses webhook failure destination when primary route falls back to delivery url alias under runtime-projected transport", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("id", std::string()) != "job-runtime-primary-url-alias-suppression") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-primary-projected-without-target" },
				{ "error", "runtime delivery failure" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "main" },
				{ "deliveryMode", "webhook" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryAttempted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-primary-url-alias-suppression" },
			{ "name", "runtime primary url alias suppression" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", {
				{ "mode", "webhook" },
				{ "url", "https://primary.example/alias" },
				{ "failureDestination", {
					{ "mode", "webhook" },
					{ "to", "https://primary.example/alias" }
				} }
			} },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	const std::size_t executed = timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(executed == 1);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("deliveryMode", std::string()) == "webhook");
	REQUIRE(runs[0].value("failureDestinationStatus", std::string()) == "suppressed");
	REQUIRE(runs[0].value("failureDestinationTarget", std::string()) == "https://primary.example/alias");
	REQUIRE(
		runs[0].value("failureDestinationError", std::string()) ==
		"failure destination matches primary delivery target");
}

TEST_CASE("Cron ops emits task-ledger hooks for scheduled terminal runs", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("scheduled-terminal-hooks");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> runningPayloads;
	std::vector<CronJson> completedPayloads;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.createRunningTaskRun = [&runningPayloads](const CronJson& payload) {
		runningPayloads.push_back(payload);
	};
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled hook ok" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hook" } } },
		{ "delivery", { { "mode", "none" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(runningPayloads.empty());
	REQUIRE_FALSE(completedPayloads.empty());
	REQUIRE(failedPayloads.empty());
	REQUIRE(runningPayloads.back().value("runtime", std::string()) == "cron");
	REQUIRE(runningPayloads.back().value("action", std::string()) == "started");
	REQUIRE(runningPayloads.back().value("lifecycleState", std::string()) == "active");
	REQUIRE(runningPayloads.back().value("taskLedgerPhase", std::string()) == "active");
	REQUIRE_FALSE(runningPayloads.back().value("taskLedgerTerminal", true));
	REQUIRE(runningPayloads.back().value("taskLedgerStatus", std::string()) == "running");
	REQUIRE(runningPayloads.back().value("taskLedgerDisposition", std::string()) == "started");
	REQUIRE(runningPayloads.back().contains("deliveryStatus"));
	REQUIRE(completedPayloads.back().value("terminal", false));
	REQUIRE(completedPayloads.back().value("action", std::string()) == "finished");
	REQUIRE(completedPayloads.back().value("lifecycleState", std::string()) == "terminal");
	REQUIRE(completedPayloads.back().value("taskLedgerPhase", std::string()) == "terminal");
	REQUIRE(completedPayloads.back().value("taskLedgerTerminal", false));
	REQUIRE(completedPayloads.back().value("taskLedgerStatus", std::string()) == "ok");
	REQUIRE(completedPayloads.back().value("disposition", std::string()) == "dispatched");
	REQUIRE(runningPayloads.back().contains("startedAtMs"));
	REQUIRE(completedPayloads.back().contains("endedAtMs"));
}

TEST_CASE("Cron ops emits task-ledger fail hook for manual terminal failure", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("manual-terminal-fail-hook");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> runningPayloads;
	std::vector<CronJson> completedPayloads;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.createRunningTaskRun = [&runningPayloads](const CronJson& payload) {
		runningPayloads.push_back(payload);
	};
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "manual hook fail" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hook" } } },
		{ "delivery", { { "mode", "webhook" }, { "to", "invalid-target" } } },
		{ "deleteAfterRun", true }
	});
	const std::string jobId = added.value("id", std::string());
	REQUIRE_FALSE(jobId.empty());

	CronJson run = ops.Run({ { "id", jobId }, { "mode", "force" } });
	REQUIRE(run.value("enqueued", false));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "manual" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(runningPayloads.empty());
	REQUIRE_FALSE(failedPayloads.empty());
	const std::string failedRunId = failedPayloads.back().value("runId", std::string());
	for (const CronJson& completedPayload : completedPayloads) {
		REQUIRE(completedPayload.value("runId", std::string()) != failedRunId);
	}
	REQUIRE(failedPayloads.back().value("runtime", std::string()) == "cron");
	REQUIRE(failedPayloads.back().value("terminal", false));
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "failed");
	REQUIRE(failedPayloads.back().value("disposition", std::string()) == "not_delivered");
	REQUIRE(failedPayloads.back().value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE(failedPayloads.back().contains("deliveryMode"));
	REQUIRE(failedPayloads.back().contains("deliveryTarget"));
	REQUIRE(failedPayloads.back().contains("queuedAtMs"));
	REQUIRE(failedPayloads.back().contains("startedAtMs"));
	REQUIRE(failedPayloads.back().contains("endedAtMs"));
}

TEST_CASE("Cron ops emits task-ledger completion hook for manual not-due terminal edge", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("manual-not-due-edge");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> completedPayloads;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson dueAdded = ops.Add({
		{ "name", "manual queued-edge not-due" },
		{ "schedule", { { "kind", "at" }, { "atMs", 4'102'444'800'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hook" } } },
		{ "delivery", { { "mode", "none" } } }
	});
	const std::string dueJobId = dueAdded.value("id", std::string());
	REQUIRE_FALSE(dueJobId.empty());

	CronJson runNotDue = ops.Run({ { "id", dueJobId }, { "mode", "due" } });
	REQUIRE_FALSE(runNotDue.value("enqueued", true));
	REQUIRE(runNotDue.value("reason", std::string()) == "not_due");
	REQUIRE(runNotDue.value("runState", std::string()) == "terminal");

	REQUIRE(failedPayloads.empty());
	REQUIRE_FALSE(completedPayloads.empty());

	bool sawNotDue = false;
	for (const auto& payload : completedPayloads) {
		const std::string disposition = payload.value("disposition", std::string());
		if (payload.value("jobId", std::string()) == dueJobId &&
			payload.value("taskLedgerStatus", std::string()) == "skipped" &&
			(disposition == "skipped" || disposition == "not_due")) {
			sawNotDue = true;
			REQUIRE(payload.value("action", std::string()) == "finished");
			REQUIRE(payload.value("lifecycleState", std::string()) == "terminal");
			REQUIRE(payload.value("phase", std::string()) == "terminal");
			REQUIRE(payload.value("terminal", false));
			REQUIRE(payload.value("deliveryStatus", std::string()) == "not-requested");
			REQUIRE(payload.value("failureDestinationStatus", std::string()) == "not-requested");
			REQUIRE(payload.value("failureAlertStatus", std::string()) == "not-requested");
			REQUIRE_FALSE(payload.value("deliveryAttempted", true));
			REQUIRE_FALSE(payload.value("failureDestinationAttempted", true));
			REQUIRE_FALSE(payload.value("failureAlertAttempted", true));
			REQUIRE(payload.value("reason", std::string()) == "not_due");
			REQUIRE(payload.contains("queuedAtMs"));
			REQUIRE(
				payload.value("summary", std::string()).find("not due") !=
				std::string::npos);
		}
	}

	REQUIRE(sawNotDue);
}

TEST_CASE("Cron ops emits task-ledger completion hook for manual unknown-job terminal edge", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("manual-unknown-job-edge");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> completedPayloads;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "manual unknown-job edge" },
		{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "hook" } } },
		{ "delivery", { { "mode", "none" } } }
	});
	const std::string removedJobId = added.value("id", std::string());
	REQUIRE_FALSE(removedJobId.empty());
	CronJson runRemoved = ops.Run({ { "id", removedJobId }, { "mode", "force" } });
	REQUIRE(runRemoved.value("enqueued", false));
	REQUIRE(runRemoved.value("reason", std::string()) == "queued");

	CronJson removed = ops.Remove({ { "id", removedJobId } });
	REQUIRE(removed.value("removed", false));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "manual" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE(failedPayloads.empty());
	REQUIRE_FALSE(completedPayloads.empty());

	bool sawUnknownJob = false;
	for (const auto& payload : completedPayloads) {
		if (payload.value("jobId", std::string()) == removedJobId &&
			payload.value("taskLedgerStatus", std::string()) == "skipped" &&
			payload.value("disposition", std::string()) == "unknown_job") {
			sawUnknownJob = true;
			REQUIRE(payload.value("action", std::string()) == "finished");
			REQUIRE(payload.value("phase", std::string()) == "terminal");
			REQUIRE(payload.value("terminal", false));
			REQUIRE(payload.value("taskLedgerDisposition", std::string()) == "unknown_job");
			REQUIRE(payload.contains("queuedAtMs"));
			REQUIRE(
				payload.value("summary", std::string()).find("no longer exists") !=
				std::string::npos);
		}
	}

	REQUIRE(sawUnknownJob);
}

TEST_CASE("Cron ops includes failure-destination suppression metadata in terminal fail hook", "[cron][ops]") {
	IsolatedCronOpsFixture fixture("failure-destination-suppression");
	CronOpsService& ops = fixture.ops();
	std::vector<CronJson> failedPayloads;
	std::vector<CronJson> completedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled suppressed delivery" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery",
			{
				{ "mode", "webhook" },
				{ "to", "https://example.test/hook" },
				{ "simulateTransientFailure", true },
				{ "failureDestination", { { "mode", "webhook" }, { "to", "https://example.test/hook" } } }
			} },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE(completedPayloads.empty());
	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "failed");
	REQUIRE(failedPayloads.back().value("disposition", std::string()) == "not_delivered");
	REQUIRE(failedPayloads.back().value("failureDestinationStatus", std::string()) == "suppressed");
}

TEST_CASE("Cron ops maps retry-scheduled delivery failure to failed disposition", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled retry-scheduled fail" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", { { "mode", "webhook" }, { "to", "https://example.test/hook" }, { "simulateHttpStatus", 503 } } },
		{ "retry", { { "maxAttempts", 2 }, { "backoffMs", CronJson::array({ 5'000 }) } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "failed");
	REQUIRE(failedPayloads.back().value("disposition", std::string()) == "failed");
	REQUIRE(failedPayloads.back().value("deliveryStatus", std::string()) == "not-delivered");
	REQUIRE(failedPayloads.back().value("retryScheduled", false));
	REQUIRE(failedPayloads.back().contains("retryScheduledAtMs"));
	REQUIRE_FALSE(failedPayloads.back()["retryScheduledAtMs"].is_null());
}

TEST_CASE("Cron ops projects failure-alert suppression metadata in terminal hook payload", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled failure-alert suppression metadata" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", {
			{ "mode", "webhook" },
			{ "to", "https://example.test/hook" },
			{ "simulateTransientFailure", true }
		} },
		{ "failureAlert", {
			{ "after", 1 },
			{ "cooldownMs", 0 },
			{ "mode", "webhook" },
			{ "to", "bad-target" }
		} },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("failureAlertSuppressed", false));
	REQUIRE(
		failedPayloads.back().value("failureAlertSuppressedReason", std::string()) ==
		"invalid_webhook_target");
	REQUIRE(failedPayloads.back().value("failureAlertMode", std::string()) == "webhook");
	REQUIRE(failedPayloads.back().value("failureAlertTarget", std::string()) == "bad-target");
}

TEST_CASE("Cron ops maps timeout error-category to timed_out terminal hook semantics", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;
	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson&, const std::int64_t)
		-> std::optional<CronJson> {
			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-timeout" },
				{ "error", "runtime timed out" },
				{ "errorCategory", "timeout" },
				{ "timedOut", true }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled timeout-category mapping" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", { { "mode", "none" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "timed_out");
	REQUIRE(failedPayloads.back().value("disposition", std::string()) == "timed_out");
	REQUIRE(failedPayloads.back().value("errorCategory", std::string()) == "timeout");
}

TEST_CASE("Cron ops maps aborted error-category to aborted terminal hook semantics", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;
	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson&, const std::int64_t)
		-> std::optional<CronJson> {
			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime-aborted" },
				{ "error", "runtime aborted" },
				{ "errorCategory", "aborted" }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "scheduled aborted-category mapping" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery", { { "mode", "none" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "aborted");
	REQUIRE(failedPayloads.back().value("disposition", std::string()) == "aborted");
	REQUIRE(failedPayloads.back().value("errorCategory", std::string()) == "aborted");
	REQUIRE(failedPayloads.back().value("aborted", false));
}

TEST_CASE("Cron timer carries runtime aborted markers into state and run logs", "[cron][timer]") {
	CronTimerService timer;
	const std::int64_t nowMs = 1'700'000'000'000;
	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson&, const std::int64_t)
		-> std::optional<CronJson> {
			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "runtime aborted" },
				{ "error", "execution aborted" },
				{ "errorCategory", "aborted" },
				{ "aborted", true }
			};
		};
	timer.SetRuntimeExecutionAdapters(std::move(adapters));

	CronJson jobs = CronJson::array({
		{
			{ "id", "job-runtime-aborted-carry-forward" },
			{ "name", "runtime aborted carry forward" },
			{ "enabled", true },
			{ "sessionTarget", "main" },
			{ "schedule", { { "kind", "every" }, { "everyMs", 60'000 } } },
			{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
			{ "delivery", { { "mode", "none" } } },
			{ "state", { { "nextRunAtMs", nowMs - 1 } } }
		}
	});
	CronJson runs = CronJson::array();

	timer.PumpDueRuns(jobs, runs, nowMs, false);
	REQUIRE(runs.size() == 1);
	REQUIRE(runs[0].value("status", std::string()) == "error");
	REQUIRE(runs[0].value("errorCategory", std::string()) == "aborted");
	REQUIRE(runs[0].value("aborted", false));
	REQUIRE(jobs[0]["state"].value("lastRunAborted", false));
}

TEST_CASE("Cron ops integrates inferred-handled runtime main-session outcomes without explicit handled flag", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> completedPayloads;
	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("name", std::string()) != "ops runtime inferred handled main") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "ok" },
				{ "summary", "ops-runtime-main-inferred-handled" },
				{ "sessionId", "main" }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops runtime inferred handled main" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "" } } },
		{ "delivery", { { "mode", "none" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(completedPayloads.empty());
	REQUIRE(completedPayloads.back().value("taskLedgerStatus", std::string()) == "ok");
	REQUIRE(completedPayloads.back().value("disposition", std::string()) == "dispatched");
	REQUIRE(completedPayloads.back().value("summary", std::string()) == "ops-runtime-main-inferred-handled");
}

TEST_CASE("Cron ops integrates inferred-handled runtime isolated outcomes without explicit handled flag", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> completedPayloads;
	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("name", std::string()) != "ops runtime inferred handled isolated") {
				return std::nullopt;
			}

			return CronJson{
				{ "status", "ok" },
				{ "summary", "ops-runtime-isolated-inferred-handled" },
				{ "sessionId", "isolated" },
				{ "sessionKey", "ops-runtime-session" }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops runtime inferred handled isolated" },
		{ "sessionTarget", "isolated" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "" } } },
		{ "delivery", { { "mode", "none" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(completedPayloads.empty());
	REQUIRE(completedPayloads.back().value("taskLedgerStatus", std::string()) == "ok");
	REQUIRE(completedPayloads.back().value("disposition", std::string()) == "dispatched");
	REQUIRE(completedPayloads.back().value("summary", std::string()) == "ops-runtime-isolated-inferred-handled");
	REQUIRE(completedPayloads.back().value("sessionKey", std::string()) == "ops-runtime-session");
}

TEST_CASE("Cron ops integrates failure-alert webhook fallback target from delivery url alias", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops failure-alert fallback url alias" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "notify" } } },
		{ "delivery",
			{
				{ "mode", "webhook" },
				{ "url", "https://example.test/delivery-alias" },
				{ "simulateTransientFailure", true }
			} },
		{ "failureAlert", { { "after", 1 }, { "cooldownMs", 0 }, { "mode", "webhook" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("taskLedgerStatus", std::string()) == "failed");
	REQUIRE(failedPayloads.back().value("failureAlertMode", std::string()) == "webhook");
	REQUIRE(failedPayloads.back().value("failureAlertTarget", std::string()) == "https://example.test/delivery-alias");
	REQUIRE(failedPayloads.back().value("failureAlertSuppressed", true) == false);
	REQUIRE(failedPayloads.back().value("failureAlertTriggered", false));
	REQUIRE(failedPayloads.back().contains("failureAlertAtMs"));
	REQUIRE_FALSE(failedPayloads.back()["failureAlertAtMs"].is_null());
}

TEST_CASE("Cron ops integrates heartbeat-busy runtime retryAfter override into terminal hook metadata", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("name", std::string()) != "ops runtime busy retryAfter") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "busy", true },
				{ "status", "error" },
				{ "summary", "ops-runtime-main-busy-retry-after" },
				{ "retryAfterMs", 3500 },
				{ "sessionId", "main" }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops runtime busy retryAfter" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", {
			{ "kind", "systemEvent" },
			{ "text", "wake" },
			{ "heartbeatBusyMaxAttempts", 3 },
			{ "heartbeatBusyDelayMs", 2000 }
		} },
		{ "delivery", {
			{ "mode", "webhook" },
			{ "to", "https://example.test/primary" },
			{ "failureDestination", {
				{ "mode", "webhook" },
				{ "to", "https://example.test/failure" }
			} }
		} },
		{ "retry", { { "maxAttempts", 3 }, { "backoffMs", CronJson::array({ 9'999 }) } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	REQUIRE(failedPayloads.back().value("errorCategory", std::string()) == "heartbeat_busy");
	REQUIRE(failedPayloads.back().value("retryScheduled", false));
	REQUIRE(failedPayloads.back().contains("retryScheduledAtMs"));
	REQUIRE(failedPayloads.back().contains("endedAtMs"));
	REQUIRE(
		failedPayloads.back().value("retryScheduledAtMs", static_cast<std::int64_t>(0)) -
		failedPayloads.back().value("endedAtMs", static_cast<std::int64_t>(0)) == 3500);
	REQUIRE(failedPayloads.back().value("deliveryStatus", std::string()) == "not-requested");
	REQUIRE(failedPayloads.back().value("failureDestinationStatus", std::string()) == "not-requested");
}

TEST_CASE("Cron ops integrates runtime projected delivery unknown status into terminal hook payload", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> completedPayloads;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.mainSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("name", std::string()) != "ops runtime delivery unknown projection") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "ok" },
				{ "summary", "ops-runtime-delivery-unknown" },
				{ "sessionId", "main" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/unknown" },
				{ "deliveryAttempted", true }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.completeTaskRunByRunId = [&completedPayloads](const CronJson& payload) {
		completedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops runtime delivery unknown projection" },
		{ "sessionTarget", "main" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "systemEvent" }, { "text", "wake" } } },
		{ "delivery", { { "mode", "webhook" }, { "to", "https://fallback.example/config" } } },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(completedPayloads.empty());
	const CronJson& payload = completedPayloads.back();
	REQUIRE(payload.value("taskLedgerStatus", std::string()) == "ok");
	REQUIRE(payload.value("disposition", std::string()) == "dispatched");
	REQUIRE(payload.value("deliveryStatus", std::string()) == "unknown");
	REQUIRE(payload.value("deliveryTarget", std::string()) == "https://runtime.example/unknown");
	REQUIRE(payload.value("summary", std::string()) == "ops-runtime-delivery-unknown");
}

TEST_CASE("Cron ops integrates runtime projected failure destination unknown status into terminal hook payload", "[cron][ops]") {
	CronOpsService ops;
	std::vector<CronJson> failedPayloads;

	blazeclaw::cron::CronRuntimeExecutionAdapters adapters;
	adapters.isolatedSession =
		[](const CronJson& job, const std::int64_t)
		-> std::optional<CronJson> {
			if (job.value("name", std::string()) != "ops runtime failure destination unknown projection") {
				return std::nullopt;
			}

			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "summary", "ops-runtime-failure-destination-unknown" },
				{ "error", "runtime transport error" },
				{ "errorCategory", "network" },
				{ "retryable", false },
				{ "sessionId", "isolated" },
				{ "deliveryStatus", "not-delivered" },
				{ "deliveryMode", "webhook" },
				{ "deliveryTarget", "https://runtime.example/primary" },
				{ "deliveryAttempted", true },
				{ "failureDestinationMode", "webhook" },
				{ "failureDestinationTarget", "https://runtime.example/failure" },
				{ "failureDestinationAttempted", true }
			};
		};
	ops.SetRuntimeExecutionAdapters(std::move(adapters));

	CronOpsService::TaskLedgerHooks hooks;
	hooks.failTaskRunByRunId = [&failedPayloads](const CronJson& payload) {
		failedPayloads.push_back(payload);
	};
	ops.SetTaskLedgerHooks(std::move(hooks));

	CronJson added = ops.Add({
		{ "name", "ops runtime failure destination unknown projection" },
		{ "sessionTarget", "isolated" },
		{ "schedule", { { "kind", "at" }, { "atMs", 1 } } },
		{ "payload", { { "kind", "agentTurn" }, { "message", "go" } } },
		{ "delivery", {
			{ "mode", "webhook" },
			{ "to", "https://config.example/primary" },
			{ "failureDestination", {
				{ "mode", "webhook" },
				{ "to", "https://config.example/failure" }
			} }
		} },
		{ "deleteAfterRun", true }
	});
	REQUIRE(added.contains("id"));

	CronJson wake = ops.Wake({ { "mode", "now" }, { "text", "run" } });
	REQUIRE(wake.value("ok", false));

	REQUIRE_FALSE(failedPayloads.empty());
	const CronJson& payload = failedPayloads.back();
	REQUIRE(payload.value("taskLedgerStatus", std::string()) == "failed");
	REQUIRE(payload.value("disposition", std::string()) == "not_delivered");
	REQUIRE(payload.value("failureDestinationStatus", std::string()) == "unknown");
	REQUIRE(payload.value("failureDestinationTarget", std::string()) == "https://runtime.example/failure");
	REQUIRE(payload.value("summary", std::string()) == "ops-runtime-failure-destination-unknown");
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

TEST_CASE(
	"Cron gateway production wiring routes add run runs and wake through handler stack",
	"[cron][gateway][wp-f]") {
	GatewayCronProductionFixture fixture("production-add-run-wake");
	GatewayHost& host = fixture.gateway();

	const ResponseFrame cronAdd = RouteGatewayCron(
		host,
		"wpf-cron-add",
		"cron.add",
		std::string(
			"{\"name\":\"wp-f gateway job\",\"enabled\":true,"
			"\"schedule\":{\"kind\":\"every\",\"everyMs\":60000},"
			"\"payload\":{\"kind\":\"systemEvent\",\"text\":\"gateway production\"},"
			"\"delivery\":{\"mode\":\"none\"}}"));
	REQUIRE(cronAdd.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.add", cronAdd));
	const CronJson cronAdded = CronJson::parse(cronAdd.payloadJson.value());
	REQUIRE(cronAdded.contains("id"));
	const std::string cronId = cronAdded.value("id", std::string());
	REQUIRE_FALSE(cronId.empty());

	const ResponseFrame cronRun = RouteGatewayCron(
		host,
		"wpf-cron-run",
		"cron.run",
		std::string("{\"id\":\"") + cronId + "\",\"mode\":\"force\"}");
	REQUIRE(cronRun.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.run", cronRun));
	const CronJson runEnvelope = CronJson::parse(cronRun.payloadJson.value());
	REQUIRE(runEnvelope.value("enqueued", false));
	REQUIRE(runEnvelope.value("reason", std::string()) == "queued");
	REQUIRE(runEnvelope.value("runState", std::string()) == "queued");
	const std::string queuedRunId = runEnvelope.value("runId", std::string());
	REQUIRE_FALSE(queuedRunId.empty());

	const ResponseFrame wakeNow = RouteGatewayCron(
		host,
		"wpf-wake-now",
		"wake",
		std::string("{\"mode\":\"now\",\"text\":\"wp-f production wake\"}"));
	REQUIRE(wakeNow.ok);
	REQUIRE(ValidateGatewayCronResponse("wake", wakeNow));

	const ResponseFrame cronRuns = RouteGatewayCron(
		host,
		"wpf-cron-runs",
		"cron.runs",
		std::string("{\"scope\":\"job\",\"id\":\"") + cronId +
		"\",\"limit\":50,\"offset\":0,\"sortDir\":\"desc\"}");
	REQUIRE(cronRuns.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.runs", cronRuns));
	const CronJson runsPayload = CronJson::parse(cronRuns.payloadJson.value());
	REQUIRE(runsPayload.contains("entries"));
	REQUIRE(runsPayload["entries"].is_array());
	REQUIRE_FALSE(runsPayload["entries"].empty());

	bool sawQueuedLifecycle = false;
	bool sawTerminalLifecycle = false;
	for (const auto& entry : runsPayload["entries"]) {
		if (!entry.is_object()) {
			continue;
		}
		if (entry.value("runId", std::string()) == queuedRunId) {
			if (entry.value("lifecycleState", std::string()) == "queued") {
				sawQueuedLifecycle = true;
			}
		}
		if (entry.value("action", std::string()) == "finished") {
			sawTerminalLifecycle = true;
		}
	}
	REQUIRE(sawQueuedLifecycle);
	REQUIRE(sawTerminalLifecycle);
}

TEST_CASE(
	"Cron gateway production wiring executes due at job on wake and validates run response",
	"[cron][gateway][wp-f]") {
	GatewayCronProductionFixture fixture("production-at-wake");
	GatewayHost& host = fixture.gateway();

	const std::int64_t nowMs = blazeclaw::cron::UtcNowMs();
	const ResponseFrame cronAdd = RouteGatewayCron(
		host,
		"wpf-cron-add-at",
		"cron.add",
		std::string("{\"name\":\"wp-f at job\",\"enabled\":true,") +
		"\"schedule\":{\"kind\":\"at\",\"atMs\":" + std::to_string(nowMs - 1) + "}," +
		"\"payload\":{\"kind\":\"systemEvent\",\"text\":\"at wake\"},"
		"\"delivery\":{\"mode\":\"none\"},\"deleteAfterRun\":true}");
	REQUIRE(cronAdd.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.add", cronAdd));
	const std::string cronId =
		CronJson::parse(cronAdd.payloadJson.value()).value("id", std::string());
	REQUIRE_FALSE(cronId.empty());

	const ResponseFrame wakeNow = RouteGatewayCron(
		host,
		"wpf-wake-at",
		"wake",
		std::string("{\"mode\":\"now\",\"text\":\"execute due at\"}"));
	REQUIRE(wakeNow.ok);
	REQUIRE(ValidateGatewayCronResponse("wake", wakeNow));

	const ResponseFrame cronRuns = RouteGatewayCron(
		host,
		"wpf-cron-runs-at",
		"cron.runs",
		std::string("{\"scope\":\"job\",\"id\":\"") + cronId +
		"\",\"limit\":20,\"offset\":0,\"sortDir\":\"desc\"}");
	REQUIRE(cronRuns.ok);
	REQUIRE(ValidateGatewayCronResponse("cron.runs", cronRuns));
	const CronJson runsPayload = CronJson::parse(cronRuns.payloadJson.value());
	REQUIRE(runsPayload["entries"].is_array());

	bool sawFinished = false;
	for (const auto& entry : runsPayload["entries"]) {
		if (!entry.is_object()) {
			continue;
		}
		if (entry.value("action", std::string()) == "finished") {
			sawFinished = true;
			REQUIRE(entry.contains("status"));
		}
	}
	REQUIRE(sawFinished);
}

TEST_CASE(
	"Cron gateway production wiring rejects invalid cron.run response after handler stack",
	"[cron][gateway][wp-f][schema]") {
	SchemaValidationIssue issue{};

	const ResponseFrame invalidRun{
		.id = "wpf-cron-run-invalid",
		.ok = true,
		.payloadJson = std::string(
			"{\"ok\":true,\"runId\":\"manual:cron-1:1:1\",\"enqueued\":false,\"started\":false,"
			"\"reason\":\"queued\",\"cronId\":\"cron-1\",\"mode\":\"force\","
			"\"queuedAtMs\":1700000000000,\"runState\":\"terminal\"}"),
		.error = std::nullopt,
	};

	REQUIRE_FALSE(GatewayProtocolSchemaValidator::ValidateResponseForMethod(
		"cron.run",
		invalidRun,
		issue));
	REQUIRE(issue.code == "schema_invalid_response");
}
