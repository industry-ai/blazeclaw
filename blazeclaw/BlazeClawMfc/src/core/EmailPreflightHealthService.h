#pragma once

#include "../gateway/executors/EmailScheduleExecutor.h"

namespace blazeclaw::core {

	class EmailPreflightHealthService {
	public:
		using RuntimeHealthIndex =
			blazeclaw::gateway::executors::RuntimeHealthIndex;

		[[nodiscard]] RuntimeHealthIndex BuildRuntimeHealthIndex(
			const bool forceRefresh = false) const {
			return blazeclaw::gateway::executors::EmailScheduleExecutor::
				GetRuntimeHealthIndex(forceRefresh);
		}
	};

} // namespace blazeclaw::core
