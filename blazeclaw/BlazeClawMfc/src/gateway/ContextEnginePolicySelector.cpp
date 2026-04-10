#include "pch.h"
#include "ContextEnginePolicySelector.h"

namespace blazeclaw::gateway {

	ContextEngineSelection ContextEnginePolicySelector::Select(
		const std::string& message,
		const std::string& failureCode) {
		ContextEngineSelection selection;
		if (message.size() > 3000 ||
			failureCode.find("overflow") != std::string::npos ||
			failureCode.find("timeout") != std::string::npos) {
			selection.engineId = "compact";
			selection.reasonCode = "context_engine_compact";
		}
		else {
			selection.reasonCode = "context_engine_default";
		}

		return selection;
	}

} // namespace blazeclaw::gateway
