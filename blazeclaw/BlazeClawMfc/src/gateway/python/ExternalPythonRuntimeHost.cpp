#include "pch.h"
#include "ExternalPythonRuntimeHost.h"

#include "../executors/PythonProcessExecutor.h"

namespace blazeclaw::gateway::python {

	const char* ExternalPythonRuntimeHost::ModeName() const {
		return "external";
	}

	bool ExternalPythonRuntimeHost::IsAvailable() const {
		return true;
	}

	ToolExecuteResult ExternalPythonRuntimeHost::Execute(
		const std::string& requestedTool,
		const std::optional<std::string>& argsJson) {
		static const auto executor =
			executors::PythonProcessExecutor::Create();
		return executor(requestedTool, argsJson);
	}

} // namespace blazeclaw::gateway::python
