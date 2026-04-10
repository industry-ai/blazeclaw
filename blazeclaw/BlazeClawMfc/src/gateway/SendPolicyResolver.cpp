#include "pch.h"
#include "SendPolicyResolver.h"

#include <algorithm>

namespace blazeclaw::gateway {

	SendPolicyDecision SendPolicyResolver::Evaluate(
		const std::string& sessionKey,
		const std::string& message,
		const bool hasAttachments,
		const std::vector<std::string>& attachmentMimeTypes) {
		SendPolicyDecision decision;

		if (sessionKey.empty()) {
			decision.allowed = false;
			decision.reasonCode = "denied_send";
			decision.policyHits.push_back("session_required");
			return decision;
		}

		if (message.empty() && !hasAttachments) {
			decision.allowed = false;
			decision.reasonCode = "denied_send";
			decision.policyHits.push_back("empty_message_without_attachments");
			return decision;
		}

		if (hasAttachments) {
			const bool hasUnsupportedMime = std::any_of(
				attachmentMimeTypes.begin(),
				attachmentMimeTypes.end(),
				[](const std::string& mime) {
					return mime.find("image/") != 0;
				});
			if (hasUnsupportedMime) {
				decision.allowed = false;
				decision.reasonCode = "denied_send";
				decision.policyHits.push_back("unsupported_attachment_mime");
				return decision;
			}
		}

		decision.policyHits.push_back("admission_allow_default");
		return decision;
	}

} // namespace blazeclaw::gateway
