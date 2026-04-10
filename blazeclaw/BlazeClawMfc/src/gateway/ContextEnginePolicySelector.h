#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct ContextEngineSelection {
		std::string engineId = "default";
		std::string reasonCode;
	};

	class ContextEnginePolicySelector {
	public:
		[[nodiscard]] static ContextEngineSelection Select(
			const std::string& message,
			const std::string& failureCode);
	};

} // namespace blazeclaw::gateway
