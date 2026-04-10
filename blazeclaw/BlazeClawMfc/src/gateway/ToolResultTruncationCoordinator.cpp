#include "pch.h"
#include "ToolResultTruncationCoordinator.h"

namespace blazeclaw::gateway {

	ToolResultTruncationResult ToolResultTruncationCoordinator::TryTruncate(
		const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& deltas,
		const std::size_t maxResultChars) {
		ToolResultTruncationResult result;
		result.deltas = deltas;
		for (auto& delta : result.deltas) {
			if (delta.phase != "tool_result") {
				continue;
			}

			if (delta.resultJson.size() <= maxResultChars) {
				continue;
			}

			delta.resultJson =
				delta.resultJson.substr(0, maxResultChars) + " ...(truncated)";
			delta.fallbackAction = "tool_result_truncation";
			result.applied = true;
		}

		return result;
	}

} // namespace blazeclaw::gateway
