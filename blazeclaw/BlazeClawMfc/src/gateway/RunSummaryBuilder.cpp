#include "pch.h"
#include "RunSummaryBuilder.h"

#include "Telemetry.h"

namespace blazeclaw::gateway {

	RunSummary RunSummaryBuilder::Build(
		const std::string& runId,
		const std::string& terminalState,
		const std::string& errorCode,
		const std::string& errorMessage,
		const std::size_t taskDeltaCount,
		const bool recovered) {
		RunSummary summary;
		summary.runId = runId;
		summary.terminalState = terminalState;
		summary.errorCode = errorCode;
		summary.errorMessage = errorMessage;
		summary.taskDeltaCount = taskDeltaCount;
		summary.recovered = recovered;
		return summary;
	}

	std::string RunSummaryBuilder::ToJson(const RunSummary& summary) {
		return std::string("{\"runId\":") + JsonString(summary.runId) +
			",\"terminalState\":" + JsonString(summary.terminalState) +
			",\"errorCode\":" + JsonString(summary.errorCode) +
			",\"errorMessage\":" + JsonString(summary.errorMessage) +
			",\"taskDeltaCount\":" + std::to_string(summary.taskDeltaCount) +
			",\"recovered\":" + std::string(summary.recovered ? "true" : "false") +
			"}";
	}

} // namespace blazeclaw::gateway
