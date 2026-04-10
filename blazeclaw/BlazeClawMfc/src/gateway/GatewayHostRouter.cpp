#include "pch.h"
#include "GatewayHostRouter.h"

namespace blazeclaw::gateway {

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

		decision.target = GatewayHostRouteTarget::StagePipeline;
		decision.reasonCode = "stage_pipeline_dynamic_default";
		return decision;
	}

} // namespace blazeclaw::gateway
