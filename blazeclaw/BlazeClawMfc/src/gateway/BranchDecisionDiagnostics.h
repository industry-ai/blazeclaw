#pragma once

#include <string>

namespace blazeclaw::gateway {

	class BranchDecisionDiagnostics {
	public:
		static void Emit(
			const std::string& runId,
			const std::string& stage,
			const std::string& branch,
			const std::string& reason,
			const std::string& detailsJson = "{}");

		static void EmitWithPayloadSummary(
			const std::string& runId,
			const std::string& stage,
			const std::string& branch,
			const std::string& reason,
			const std::string& payload,
			std::size_t maxChars = 256);
	};

} // namespace blazeclaw::gateway
