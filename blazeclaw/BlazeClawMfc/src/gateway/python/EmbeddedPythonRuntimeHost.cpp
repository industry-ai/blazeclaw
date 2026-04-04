#include "pch.h"
#include "EmbeddedPythonRuntimeHost.h"

namespace blazeclaw::gateway::python {

	namespace {
		std::string BuildUnavailableEnvelope() {
			return "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\","
				"\"runtimeMode\":\"embedded\",\"output\":[],\"requiresApproval\":null,"
				"\"error\":{\"code\":\"python_embedded_runtime_unavailable\","
				"\"message\":\"embedded python runtime host is not implemented\"}}";
		}
	}

	const char* EmbeddedPythonRuntimeHost::ModeName() const {
		return "embedded";
	}

	bool EmbeddedPythonRuntimeHost::IsAvailable() const {
		return false;
	}

	ToolExecuteResult EmbeddedPythonRuntimeHost::Execute(
		const std::string& requestedTool,
		const std::optional<std::string>& argsJson) {
		UNREFERENCED_PARAMETER(argsJson);
		return ToolExecuteResult{
			.tool = requestedTool,
			.executed = false,
			.status = "error",
			.output = BuildUnavailableEnvelope(),
		};
	}

} // namespace blazeclaw::gateway::python
