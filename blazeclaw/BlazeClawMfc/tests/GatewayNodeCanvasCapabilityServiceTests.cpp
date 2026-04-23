#include "gateway/GatewayNodeCanvasCapabilityService.h"

#include <catch2/catch_all.hpp>

#include <thread>

using blazeclaw::gateway::GatewayNodeCanvasCapabilityService;

TEST_CASE("GatewayNodeCanvasCapabilityService verifies minted capability", "[gateway][http-auth][phase4]") {
	GatewayNodeCanvasCapabilityService service;
	const auto refreshed = service.RefreshCapability("session-a", "http://localhost:8080/__openclaw__/canvas");
	REQUIRE(refreshed.ok);
	REQUIRE_FALSE(refreshed.canvasCapability.empty());

	REQUIRE(service.VerifyCapabilityAndRefreshTtl(refreshed.canvasCapability));
}

TEST_CASE("GatewayNodeCanvasCapabilityService rejects unknown capability", "[gateway][http-auth][phase4]") {
	GatewayNodeCanvasCapabilityService service;
	REQUIRE_FALSE(service.VerifyCapabilityAndRefreshTtl("canvas-capability-unknown"));
}

TEST_CASE("GatewayNodeCanvasCapabilityService capability refresh keeps verification valid", "[gateway][http-auth][phase4]") {
	GatewayNodeCanvasCapabilityService service;
	const auto refreshed = service.RefreshCapability("session-b", "http://localhost:8080/__openclaw__/canvas");
	REQUIRE(refreshed.ok);

	REQUIRE(service.VerifyCapabilityAndRefreshTtl(refreshed.canvasCapability));
	std::this_thread::sleep_for(std::chrono::milliseconds(2));
	REQUIRE(service.VerifyCapabilityAndRefreshTtl(refreshed.canvasCapability));
}
