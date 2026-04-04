#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::python {

	class PythonRuntimeDispatcher {
	public:
		static GatewayToolRegistry::RuntimeToolExecutor CreateExecutor();
		static GatewayToolRegistry::RuntimeToolExecutor CreateDiagnosticsExecutor();
		static std::string BuildRuntimeDiagnosticsJson();
	};

} // namespace blazeclaw::gateway::python
