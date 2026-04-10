#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct GatewayHostRouteRequest {
		std::string method;
		std::string orchestrationPath;
		bool stageHostHealthy = false;
		bool runtimeOrchestrationCompatEnabled = false;
		bool stagePipelineFeatureEnabled = true;
		std::string rolloutCohort;
	};

	enum class GatewayHostRouteTarget {
		Legacy,
		StagePipeline,
	};

	struct GatewayHostRouteDecision {
		GatewayHostRouteTarget target = GatewayHostRouteTarget::Legacy;
		std::string reasonCode = "legacy_default";
		bool fallback = false;
		std::string selectedCohort;
	};

	class GatewayHostRouter {
	public:
		[[nodiscard]] GatewayHostRouteDecision Decide(
			const GatewayHostRouteRequest& request) const;
	};

} // namespace blazeclaw::gateway
