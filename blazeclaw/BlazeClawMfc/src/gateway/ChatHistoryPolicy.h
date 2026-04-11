#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

	class ChatHistoryPolicy {
	public:
		struct BuildParams {
			std::vector<std::string> history;
			std::size_t requestedLimit = 200;
		};

		struct BuildResult {
			std::string messagesJson = "[]";
			std::size_t placeholderCount = 0;
		};

		[[nodiscard]] BuildResult Build(const BuildParams& params) const;
	};

} // namespace blazeclaw::gateway
