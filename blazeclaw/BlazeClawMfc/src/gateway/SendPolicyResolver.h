#pragma once

#include <string>
#include <vector>

namespace blazeclaw::gateway {

	struct SendPolicyDecision {
		bool allowed = true;
		std::string reasonCode = "allowed_send";
		std::vector<std::string> policyHits;
	};

	class SendPolicyResolver {
	public:
		[[nodiscard]] static SendPolicyDecision Evaluate(
			const std::string& sessionKey,
			const std::string& message,
			bool hasAttachments,
			const std::vector<std::string>& attachmentMimeTypes);
	};

} // namespace blazeclaw::gateway
