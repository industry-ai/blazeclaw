#pragma once

#include <string>

namespace blazeclaw::gateway {

	enum class GatewayHostRouteTarget {
		Legacy,
		StagePipeline,
	};

	struct GatewayHostRouteDecision {
		GatewayHostRouteTarget target = GatewayHostRouteTarget::Legacy;
		std::string reasonCode = "legacy_default";
		bool fallback = false;
	};

	class GatewayHostRouter {
	public:
		[[nodiscard]] GatewayHostRouteDecision Decide(
			const std::string& method,
			const std::string& orchestrationPath,
			bool stageHostHealthy) const;
	};

} // namespace blazeclaw::gateway
