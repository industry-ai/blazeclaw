#include "pch.h"
#include "GatewayHostRouter.h"

#include <functional>

namespace blazeclaw::gateway {
	namespace {
		bool IsCanaryBucket(const std::string& requestId) {
			if (requestId.empty()) {
				return false;
			}

			const std::size_t bucket =
				std::hash<std::string>{}(requestId) % 100;
			return bucket < 20;
		}
	}

	GatewayHostRouteDecision GatewayHostRouter::Decide(
		const GatewayHostRouteRequest& request) const {
		GatewayHostRouteDecision decision;
		decision.selectedCohort = request.rolloutCohort;

		if (request.method != "chat.send") {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_non_chat_send";
			return decision;
		}

		if (!request.stagePipelineFeatureEnabled) {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_stage_pipeline_feature_disabled";
			return decision;
		}

		if (!request.stageHostHealthy) {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "fallback_stage_host_unhealthy";
			decision.fallback = true;
			return decision;
		}

		if (request.orchestrationPath == "runtime_orchestration" ||
			request.runtimeOrchestrationCompatEnabled) {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_runtime_orchestration_compat";
			return decision;
		}

		if (request.rolloutCohort == "legacy_only" ||
			request.rolloutCohort == "stage_pipeline_off") {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_rollout_cohort_off";
			return decision;
		}

		if (request.rolloutCohort == "canary") {
			if (!IsCanaryBucket(request.requestId)) {
				decision.target = GatewayHostRouteTarget::Legacy;
				decision.reasonCode = "legacy_canary_holdback";
				return decision;
			}

			decision.target = GatewayHostRouteTarget::StagePipeline;
			decision.reasonCode = "stage_pipeline_canary_bucket";
			return decision;
		}

		if (request.rolloutCohort == "full" ||
			request.rolloutCohort == "stage_pipeline_full") {
			decision.target = GatewayHostRouteTarget::StagePipeline;
			decision.reasonCode = "stage_pipeline_full_rollout";
			return decision;
		}

		decision.target = GatewayHostRouteTarget::StagePipeline;
		decision.reasonCode = "stage_pipeline_dynamic_default";
		return decision;
	}

} // namespace blazeclaw::gateway
