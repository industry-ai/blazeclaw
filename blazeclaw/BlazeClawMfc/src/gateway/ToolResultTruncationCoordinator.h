#pragma once

#include "GatewayHost.h"

#include <vector>

namespace blazeclaw::gateway {

	struct ToolResultTruncationResult {
		bool applied = false;
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> deltas;
	};

	class ToolResultTruncationCoordinator {
	public:
		[[nodiscard]] static ToolResultTruncationResult TryTruncate(
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& deltas,
			std::size_t maxResultChars = 512);
	};

} // namespace blazeclaw::gateway
