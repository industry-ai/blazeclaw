#include "pch.h"
#include "TranscriptPolicyResolver.h"

#include <algorithm>

namespace blazeclaw::gateway {

	TranscriptPolicyDecision TranscriptPolicyResolver::Resolve(
		const std::string& message,
		const std::string& modelHint) {
		TranscriptPolicyDecision decision;
		decision.sanitizedMessage = message;

		if (decision.sanitizedMessage.empty()) {
			decision.reasonCode = "transcript_policy_none";
			return decision;
		}

		if (!modelHint.empty()) {
			std::string loweredModel = modelHint;
			std::transform(
				loweredModel.begin(),
				loweredModel.end(),
				loweredModel.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			if (loweredModel.find("deepseek") != std::string::npos) {
				const std::string thinkOpen = "<think>";
				const std::string thinkClose = "</think>";
				const auto openPos = decision.sanitizedMessage.find(thinkOpen);
				if (openPos != std::string::npos) {
					const auto closePos = decision.sanitizedMessage.find(
						thinkClose,
						openPos + thinkOpen.size());
					if (closePos != std::string::npos) {
						decision.sanitizedMessage.erase(
							openPos,
							(closePos + thinkClose.size()) - openPos);
						decision.applied = true;
						decision.reasonCode = "transcript_policy_applied";
					}
				}
			}
		}

		if (decision.sanitizedMessage.size() > 8000) {
			decision.sanitizedMessage = decision.sanitizedMessage.substr(0, 8000);
			decision.applied = true;
			decision.reasonCode = "transcript_policy_applied";
		}

		return decision;
	}

} // namespace blazeclaw::gateway
