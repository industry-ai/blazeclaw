#pragma once

#include "IPythonRuntimeHost.h"

namespace blazeclaw::gateway::python {

	struct EmbeddedPythonRuntimeHealth {
		bool runtimeLoaded = false;
		bool interpreterInitialized = false;
		bool available = false;
		std::string lastError;
		std::string loadedModule;
	};

	class EmbeddedPythonRuntimeHost : public IPythonRuntimeHost {
	public:
		const char* ModeName() const override;
		bool IsAvailable() const override;
		static EmbeddedPythonRuntimeHealth ProbeHealth();
		ToolExecuteResult Execute(
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson) override;
	};

} // namespace blazeclaw::gateway::python
