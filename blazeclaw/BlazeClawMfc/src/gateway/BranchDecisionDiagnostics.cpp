#include "pch.h"
#include "BranchDecisionDiagnostics.h"

#include "Telemetry.h"

namespace blazeclaw::gateway {
	namespace {
		std::string TruncateForSummary(
			const std::string& payload,
			const std::size_t maxChars) {
			if (payload.size() <= maxChars) {
				return payload;
			}

			if (maxChars <= 3) {
				return payload.substr(0, maxChars);
			}

			return payload.substr(0, maxChars - 3) + "...";
		}
	}

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

	void BranchDecisionDiagnostics::EmitWithPayloadSummary(
		const std::string& runId,
		const std::string& stage,
		const std::string& branch,
		const std::string& reason,
		const std::string& payload,
		const std::size_t maxChars) {
		Emit(
			runId,
			stage,
			branch,
			reason,
			std::string("{\"payloadChars\":") +
			std::to_string(payload.size()) +
			",\"payloadSummary\":" +
			JsonString(TruncateForSummary(payload, maxChars)) +
			"}");
	}

} // namespace blazeclaw::gateway
