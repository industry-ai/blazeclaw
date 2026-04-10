#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	struct ToolPolicyDecision {
		bool allowAll = true;
		std::vector<std::string> allowedTargets;
		std::string reasonCode = "tool_policy_allow_all";
	};

	class ToolPolicyPipeline {
	public:
		[[nodiscard]] static ToolPolicyDecision Build(
			bool enforceOrderedAllowlist,
			const std::vector<std::string>& orderedAllowedToolTargets,
			const std::vector<ToolCatalogEntry>& runtimeTools);
	};

} // namespace blazeclaw::gateway
