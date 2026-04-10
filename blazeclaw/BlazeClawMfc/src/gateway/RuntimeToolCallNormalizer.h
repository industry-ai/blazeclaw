#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	class RuntimeToolCallNormalizer {
	public:
		[[nodiscard]] static std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			EnsureRuntimeTaskDeltas(
				const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
				const std::string& runId,
				const std::string& sessionKey,
				bool success,
				const std::string& assistantText,
				const std::string& errorCode,
				const std::string& errorMessage);

		[[nodiscard]] static std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			ApplyInvalidArgumentsRecoveryPolicy(
				const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& source,
				const std::string& runId,
				const std::string& sessionKey,
				const std::string& message,
				GatewayToolRegistry& toolRegistry);
	};

} // namespace blazeclaw::gateway
