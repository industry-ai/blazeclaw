#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	class TaskDeltaLegacyAdapter {
	public:
		[[nodiscard]] static GatewayHost::ChatRuntimeResult::TaskDeltaEntry AdaptEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionId,
			std::size_t index);

		[[nodiscard]] static std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> AdaptRun(
			const std::string& runId,
			const std::string& sessionId,
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& entries);
	};

} // namespace blazeclaw::gateway
