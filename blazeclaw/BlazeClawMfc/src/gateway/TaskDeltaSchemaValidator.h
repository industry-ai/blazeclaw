#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	class TaskDeltaSchemaValidator {
	public:
		[[nodiscard]] static bool ValidateEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& entry,
			std::string& errorCode,
			std::string& errorMessage);

		[[nodiscard]] static bool ValidateRun(
			const std::string& runId,
			const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& entries,
			std::string& errorCode,
			std::string& errorMessage);
	};

} // namespace blazeclaw::gateway
