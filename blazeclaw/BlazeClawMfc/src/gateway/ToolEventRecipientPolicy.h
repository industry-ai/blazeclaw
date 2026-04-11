#pragma once

#include <string>
#include <vector>

namespace blazeclaw::gateway {

	class ToolEventRecipientPolicy {
	public:
		struct Input {
			std::vector<std::string> clientCaps;
			std::string sessionKey;
			std::string runId;
		};

		struct Output {
			bool wantsToolEvents = false;
			std::string reasonCode = "capability_missing";
		};

		[[nodiscard]] Output Evaluate(const Input& input) const;
	};

} // namespace blazeclaw::gateway
