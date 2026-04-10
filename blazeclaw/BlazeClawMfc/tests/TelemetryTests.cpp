#include "gateway/Telemetry.h"
#include "gateway/RunSummaryBuilder.h"
#include "gateway/GatewayLifecycleEventEmitter.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::NormalizePayloadObject;
using blazeclaw::gateway::JsonString;
using blazeclaw::gateway::BuildGatewayHostRouteDecisionPayload;
using blazeclaw::gateway::RunSummaryBuilder;
using blazeclaw::gateway::GatewayLifecycleEventEmitter;

TEST_CASE("NormalizePayloadObject keeps object payload shape", "[telemetry]") {
	const std::string payload = "  {\"key\":\"value\",\"n\":1}  ";
	const std::string normalized = NormalizePayloadObject(payload);

	REQUIRE(normalized == "{\"key\":\"value\",\"n\":1}");
}

TEST_CASE("NormalizePayloadObject wraps non-object payload as raw", "[telemetry]") {
	const std::string payload = "not-json-object";
	const std::string normalized = NormalizePayloadObject(payload);

	REQUIRE(normalized == "{\"raw\":\"not-json-object\"}");
}

TEST_CASE("NormalizePayloadObject returns empty object for whitespace", "[telemetry]") {
	const std::string payload = " \t\r\n  ";
	const std::string normalized = NormalizePayloadObject(payload);

	REQUIRE(normalized == "{}");
}

TEST_CASE("JsonString escapes quotes and newlines", "[telemetry]") {
	const std::string input = "line \"quoted\"\nnext line";
	const std::string escaped = JsonString(input);

	REQUIRE(escaped == "\"line \\\"quoted\\\"\\nnext line\"");
}

TEST_CASE("RunSummaryBuilder serializes terminal summary envelope", "[telemetry][run-summary]") {
	const auto summary = RunSummaryBuilder::Build(
		"run-1",
		"failed",
		"timeout",
		"runtime timed out",
		5,
		false);
	const std::string payload = RunSummaryBuilder::ToJson(summary);

	REQUIRE(payload.find("\"runId\":\"run-1\"") != std::string::npos);
	REQUIRE(payload.find("\"terminalState\":\"failed\"") != std::string::npos);
	REQUIRE(payload.find("\"taskDeltaCount\":5") != std::string::npos);
}

TEST_CASE("GatewayLifecycleEventEmitter builds lifecycle payload", "[telemetry][lifecycle]") {
	const std::string payload = GatewayLifecycleEventEmitter::BuildLifecyclePayload(
		"run-2",
		"main",
		"queued",
		123,
		std::optional<std::string>("preflight"));

	REQUIRE(payload.find("\"runId\":\"run-2\"") != std::string::npos);
	REQUIRE(payload.find("\"state\":\"queued\"") != std::string::npos);
	REQUIRE(payload.find("\"reason\":\"preflight\"") != std::string::npos);
}

TEST_CASE("Route decision telemetry payload keeps object shape", "[telemetry][router]") {
	const std::string payload = NormalizePayloadObject(
		"{\"method\":\"chat.send\",\"target\":\"stage_pipeline\",\"reason\":\"stage_pipeline_dynamic_default\",\"fallback\":false}");

	REQUIRE(payload.find("\"method\":\"chat.send\"") != std::string::npos);
	REQUIRE(payload.find("\"target\":\"stage_pipeline\"") != std::string::npos);
	REQUIRE(payload.find("\"fallback\":false") != std::string::npos);
}

TEST_CASE("BuildGatewayHostRouteDecisionPayload emits deterministic fields", "[telemetry][router]") {
	const std::string payload = BuildGatewayHostRouteDecisionPayload(
		"chat.send",
		"legacy",
		"legacy_stage_pipeline_feature_disabled",
		"legacy_only",
		false);

	REQUIRE(payload.find("\"method\":\"chat.send\"") != std::string::npos);
	REQUIRE(payload.find("\"target\":\"legacy\"") != std::string::npos);
	REQUIRE(payload.find("\"reason\":\"legacy_stage_pipeline_feature_disabled\"") != std::string::npos);
	REQUIRE(payload.find("\"cohort\":\"legacy_only\"") != std::string::npos);
	REQUIRE(payload.find("\"fallback\":false") != std::string::npos);
}

TEST_CASE("BuildGatewayHostRouteDecisionPayload normalizes through telemetry payload object", "[telemetry][router]") {
	const std::string routePayload = BuildGatewayHostRouteDecisionPayload(
		"chat.send",
		"stage_pipeline",
		"stage_pipeline_dynamic_default",
		"canary",
		false);

	const std::string normalized = NormalizePayloadObject(routePayload);
	REQUIRE(normalized.find("\"method\":\"chat.send\"") != std::string::npos);
	REQUIRE(normalized.find("\"target\":\"stage_pipeline\"") != std::string::npos);
	REQUIRE(normalized.find("\"cohort\":\"canary\"") != std::string::npos);
}
