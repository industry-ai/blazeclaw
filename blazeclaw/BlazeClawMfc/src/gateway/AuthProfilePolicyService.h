#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct AuthProfileDecision {
		std::string selectedProfileId = "default";
		bool fallbackApplied = false;
		std::string reasonCode;
	};

	class AuthProfilePolicyService {
	public:
		[[nodiscard]] static AuthProfileDecision SelectFallbackProfile(
			const std::string& currentProfileId,
			const std::string& failureCode);
	};

} // namespace blazeclaw::gateway
