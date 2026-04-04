#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::python {

	class PythonRuntimeDispatcher {
	public:
		static GatewayToolRegistry::RuntimeToolExecutor CreateExecutor();
	};

} // namespace blazeclaw::gateway::python
