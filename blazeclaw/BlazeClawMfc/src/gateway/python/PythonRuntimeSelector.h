#pragma once

#include <optional>
#include <string>

namespace blazeclaw::gateway::python {

	struct PythonRuntimeSelection {
		bool resolved = false;
		std::string mode;
		std::string modeSource;
		bool strictMode = true;
		bool allowFallbackToExternal = false;
		std::string errorCode;
		std::string errorMessage;
	};

	class PythonRuntimeSelector {
	public:
		PythonRuntimeSelection Resolve(
			const std::optional<std::string>& argsJson) const;
	};

} // namespace blazeclaw::gateway::python
