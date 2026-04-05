#pragma once

#include "../GatewayToolRegistry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blazeclaw::gateway::executors {

	struct DependencyProbeResult {
		std::string key;
		std::string state;
		std::string reasonCode;
		std::string reasonMessage;
		std::uint64_t checkedAtEpochMs = 0;
		std::uint64_t expiresAtEpochMs = 0;
	};

	struct RuntimeHealthIndex {
		std::vector<DependencyProbeResult> probes;
		std::string emailSendState;
		std::uint64_t generatedAtEpochMs = 0;
		std::uint64_t ttlMs = 0;
	};

	class EmailScheduleExecutor {
	public:
		static GatewayToolRegistry::RuntimeToolExecutor Create();
		static RuntimeHealthIndex GetRuntimeHealthIndex(bool forceRefresh = false);
	};

} // namespace blazeclaw::gateway::executors
