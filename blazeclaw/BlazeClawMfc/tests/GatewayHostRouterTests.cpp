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
	  .requestId = "req-3",
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "canary",
		});
	const bool isStageTarget =
		stageDecisionAgain.target == GatewayHostRouteTarget::StagePipeline;
	const bool isLegacyTarget =
		stageDecisionAgain.target == GatewayHostRouteTarget::Legacy;
	REQUIRE((isStageTarget || isLegacyTarget));
	const bool isCanaryRouted =
		stageDecisionAgain.reasonCode == "stage_pipeline_canary_bucket";
	const bool isCanaryHoldback =
		stageDecisionAgain.reasonCode == "legacy_canary_holdback";
	const bool validCanaryDecision = isCanaryRouted || isCanaryHoldback;
	REQUIRE((validCanaryDecision));
}

TEST_CASE(
	"GatewayHostRouter keeps legacy when rollout cohort is explicit off",
	"[router][rollout]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.requestId = "req-off",
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "stage_pipeline_off",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::Legacy);
	REQUIRE(decision.reasonCode == "legacy_rollout_cohort_off");
}

TEST_CASE(
	"GatewayHostRouter selects stage pipeline for full rollout cohort",
	"[router][rollout]") {
	GatewayHostRouter router;
	const auto decision = router.Decide(GatewayHostRouteRequest{
		.requestId = "req-full",
		.method = "chat.send",
		.orchestrationPath = "dynamic_task_delta",
		.stageHostHealthy = true,
		.runtimeOrchestrationCompatEnabled = false,
		.stagePipelineFeatureEnabled = true,
		.rolloutCohort = "stage_pipeline_full",
		});

	REQUIRE(decision.target == GatewayHostRouteTarget::StagePipeline);
	REQUIRE(decision.reasonCode == "stage_pipeline_full_rollout");
}
