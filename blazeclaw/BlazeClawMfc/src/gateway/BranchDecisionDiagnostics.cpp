#include "pch.h"
#include "BranchDecisionDiagnostics.h"

#include "Telemetry.h"

namespace blazeclaw::gateway {

	void BranchDecisionDiagnostics::Emit(
		const std::string& runId,
		const std::string& stage,
		const std::string& branch,
		const std::string& reason,
		const std::string& detailsJson) {
		EmitTelemetryEvent(
			"gateway.chat.branchDecision",
			std::string("{\"runId\":") + JsonString(runId) +
			",\"stage\":" + JsonString(stage) +
			",\"branch\":" + JsonString(branch) +
			",\"reason\":" + JsonString(reason) +
			",\"details\": " +
			(detailsJson.empty() ? std::string("{}") : detailsJson) +
			"}");
	}

} // namespace blazeclaw::gateway
