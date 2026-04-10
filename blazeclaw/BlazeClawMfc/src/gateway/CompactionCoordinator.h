#pragma once

#include <string>

namespace blazeclaw::gateway {

	struct CompactionResult {
		bool applied = false;
		std::string compactedMessage;
		std::string reasonCode;
	};

	class CompactionCoordinator {
	public:
		[[nodiscard]] static CompactionResult TryCompact(
			const std::string& message,
			const std::string& errorCode,
			std::size_t maxChars = 1200);
	};

} // namespace blazeclaw::gateway
