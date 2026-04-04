#pragma once

#include "../GatewayToolRegistry.h"

namespace blazeclaw::gateway::python {

	class IPythonRuntimeHost {
	public:
		virtual ~IPythonRuntimeHost() = default;

		virtual const char* ModeName() const = 0;
		virtual bool IsAvailable() const = 0;
		virtual ToolExecuteResult Execute(
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson) = 0;
	};

} // namespace blazeclaw::gateway::python
