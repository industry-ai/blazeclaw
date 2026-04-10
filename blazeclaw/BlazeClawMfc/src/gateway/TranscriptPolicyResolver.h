#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct TranscriptPolicyDecision {
		std::string sanitizedMessage;
		bool applied = false;
		std::string reasonCode = "transcript_policy_none";
	};

	class TranscriptPolicyResolver {
	public:
		[[nodiscard]] static TranscriptPolicyDecision Resolve(
			const std::string& message,
			const std::string& modelHint = "");
	};

} // namespace blazeclaw::gateway
