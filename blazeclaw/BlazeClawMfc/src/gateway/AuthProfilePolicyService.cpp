#include "pch.h"
#include "AuthProfilePolicyService.h"

namespace blazeclaw::gateway {

	AuthProfileDecision AuthProfilePolicyService::SelectFallbackProfile(
		const std::string& currentProfileId,
		const std::string& failureCode) {
		AuthProfileDecision decision;
		decision.selectedProfileId = currentProfileId.empty()
			? "default"
			: currentProfileId;

		if (failureCode.find("auth") != std::string::npos) {
			decision.selectedProfileId = "backup";
			decision.fallbackApplied = true;
			decision.reasonCode = "profile_fallback_auth";
		}
		else {
			decision.reasonCode = "profile_stay";
		}

		return decision;
	}

} // namespace blazeclaw::gateway
