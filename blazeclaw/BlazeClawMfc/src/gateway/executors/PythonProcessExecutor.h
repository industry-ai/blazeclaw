#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::executors {

	class PythonProcessExecutor {
	public:
		static GatewayToolRegistry::RuntimeToolExecutor Create();
	};

} // namespace blazeclaw::gateway::executors
