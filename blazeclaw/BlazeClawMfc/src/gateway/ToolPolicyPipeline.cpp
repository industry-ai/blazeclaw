#include "pch.h"
#include "ToolPolicyPipeline.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
		std::string ToLowerCopyToolPolicy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}
	}

	ToolPolicyDecision ToolPolicyPipeline::Build(
		const bool enforceOrderedAllowlist,
		const std::vector<std::string>& orderedAllowedToolTargets,
		const std::vector<ToolCatalogEntry>& runtimeTools) {
		ToolPolicyDecision decision;
		if (!enforceOrderedAllowlist) {
			decision.reasonCode = "tool_policy_allow_all";
			decision.allowAll = true;
			return decision;
		}

		decision.allowAll = false;
		decision.reasonCode = "tool_policy_allowlist";
		for (const auto& requested : orderedAllowedToolTargets) {
			const std::string requestedLower = ToLowerCopyToolPolicy(requested);
			auto it = std::find_if(
				runtimeTools.begin(),
				runtimeTools.end(),
				[&requestedLower](const ToolCatalogEntry& tool) {
					return tool.enabled && ToLowerCopyToolPolicy(tool.id) == requestedLower;
				});
			if (it != runtimeTools.end()) {
				decision.allowedTargets.push_back(it->id);
			}
		}

		if (decision.allowedTargets.empty()) {
			decision.reasonCode = "tool_policy_block";
		}

		return decision;
	}

} // namespace blazeclaw::gateway
