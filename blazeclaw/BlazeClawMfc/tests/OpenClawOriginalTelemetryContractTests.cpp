#include "gateway/Telemetry.h"

#include <catch2/catch_all.hpp>

TEST_CASE("OpenClaw-original telemetry payload shape stays object", "[telemetry][openclaw-original]") {
	const std::string payload =
		"{\"skill\":\"nano-pdf\",\"state\":\"tool_enabled\",\"diagnostics\":1}";
	const std::string normalized = blazeclaw::gateway::NormalizePayloadObject(payload);

	REQUIRE(normalized.find("\"skill\":\"nano-pdf\"") != std::string::npos);
	REQUIRE(normalized.find("\"state\":\"tool_enabled\"") != std::string::npos);
	REQUIRE(normalized.find("\"diagnostics\":1") != std::string::npos);
}

TEST_CASE("OpenClaw-original event names remain stable", "[telemetry][openclaw-original]") {
	const std::vector<std::string> events = {
		"skills.openclaw_original.detected",
		"skills.openclaw_original.imported",
		"skills.openclaw_original.tool_enabled",
		"skills.openclaw_original.failed",
	};

	for (const auto& eventName : events) {
		const std::string frame = std::string("{\"event\":") +
			blazeclaw::gateway::JsonString(eventName) + "}";
		const std::string normalized = blazeclaw::gateway::NormalizePayloadObject(frame);
		REQUIRE(normalized.find(eventName) != std::string::npos);
	}
}
