#pragma once

#include "IPythonRuntimeHost.h"

namespace blazeclaw::gateway::python {

	class EmbeddedPythonRuntimeHost : public IPythonRuntimeHost {
	public:
		const char* ModeName() const override;
		bool IsAvailable() const override;
		ToolExecuteResult Execute(
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson) override;
	};

} // namespace blazeclaw::gateway::python
