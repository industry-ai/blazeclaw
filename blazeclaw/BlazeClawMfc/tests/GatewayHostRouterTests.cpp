#include "gateway/GatewayHostRouter.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("GatewayHostRouter selects stage pipeline for dynamic chat.send", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "cohort-a",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::StagePipeline);
	REQUIRE(decision.reasonCode == "stage_pipeline_dynamic_default");
	REQUIRE_FALSE(decision.fallback);
	REQUIRE(decision.selectedCohort == "cohort-a");
}

TEST_CASE("GatewayHostRouter keeps legacy for runtime orchestration compatibility", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "runtime_orchestration",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = true,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "default",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "legacy_runtime_orchestration_compat");
}

TEST_CASE("GatewayHostRouter falls back when stage host unhealthy", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = false,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "default",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.fallback);
	REQUIRE(decision.reasonCode == "fallback_stage_host_unhealthy");
}

TEST_CASE("GatewayHostRouter keeps legacy when stage pipeline feature is disabled", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = false,
		.rolloutCohort = "off",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "legacy_stage_pipeline_feature_disabled");
	REQUIRE_FALSE(decision.fallback);
}

TEST_CASE("GatewayHostRouter falls back when stage host reports unhealthy", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = false,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "default",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "fallback_stage_host_unhealthy");
	REQUIRE(decision.fallback);
}

TEST_CASE("GatewayHostRouter keeps legacy for non-chat requests", "[router]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.method = "gateway.tools.list",
		.orchestrationPath = "stage_pipeline_canary",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "canary",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "legacy_non_chat_send");
	REQUIRE_FALSE(decision.fallback);
	REQUIRE(decision.selectedCohort == "canary");
}

TEST_CASE("GatewayHostRouter decisions are reversible for route mode switches", "[router]") {
	GatewayHostRouter router;

	const auto stageDecision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "canary",
		});
	REQUIRE(stageDecision.target == GatewayHostRouteTarget::StagePipeline);

	const auto legacyDecision = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = false,
		.rolloutCohort = "legacy_only",
		});
	REQUIRE(legacyDecision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(legacyDecision.reasonCode == "legacy_stage_pipeline_feature_disabled");

	const auto stageDecisionAgain = router.Decide(GatewayHostRouteRequest{
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "canary",
		});
	REQUIRE(stageDecisionAgain.target == GatewayHostRouteTarget::StagePipeline);
	REQUIRE(stageDecisionAgain.reasonCode == "stage_pipeline_dynamic_default");
}
