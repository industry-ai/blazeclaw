#include "pch.h"
#include "GatewayHostRouter.h"

namespace blazeclaw::gateway {

	GatewayHostRouteDecision GatewayHostRouter::Decide(
		const std::string& method,
		const std::string& orchestrationPath,
		const bool stageHostHealthy) const {
		GatewayHostRouteDecision decision;

		if (method != "chat.send") {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_non_chat_send";
			return decision;
		}

		if (!stageHostHealthy) {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "fallback_stage_host_unhealthy";
			decision.fallback = true;
			return decision;
		}

		if (orchestrationPath == "runtime_orchestration") {
			decision.target = GatewayHostRouteTarget::Legacy;
			decision.reasonCode = "legacy_runtime_orchestration_compat";
			return decision;
		}

		decision.target = GatewayHostRouteTarget::StagePipeline;
		decision.reasonCode = "stage_pipeline_dynamic_default";
		return decision;
	}

} // namespace blazeclaw::gateway
