#include "gateway/GatewayHostRouter.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("GatewayHostRouter selects stage pipeline for dynamic chat.send", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(
		"chat.send",
		"dynamic_task_delta",
		true);

	REQUIRE(decision.target == GatewayHostRouteTarget::StagePipeline);
	REQUIRE(decision.reasonCode == "stage_pipeline_dynamic_default");
	REQUIRE_FALSE(decision.fallback);
}

TEST_CASE("GatewayHostRouter keeps legacy for runtime orchestration compatibility", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(
		"chat.send",
		"runtime_orchestration",
		true);

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "legacy_runtime_orchestration_compat");
}

TEST_CASE("GatewayHostRouter falls back when stage host unhealthy", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(
		"chat.send",
		"dynamic_task_delta",
		false);

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.fallback);
	REQUIRE(decision.reasonCode == "fallback_stage_host_unhealthy");
}
