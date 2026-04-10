#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct RunSummary {
		std::string runId;
		std::string terminalState;
		std::string errorCode;
		std::string errorMessage;
		std::size_t taskDeltaCount = 0;
		bool recovered = false;
	};

	class RunSummaryBuilder {
	public:
		[[nodiscard]] static RunSummary Build(
			const std::string& runId,
			const std::string& terminalState,
			const std::string& errorCode,
			const std::string& errorMessage,
			std::size_t taskDeltaCount,
			bool recovered);

		[[nodiscard]] static std::string ToJson(const RunSummary& summary);
	};

} // namespace blazeclaw::gateway
