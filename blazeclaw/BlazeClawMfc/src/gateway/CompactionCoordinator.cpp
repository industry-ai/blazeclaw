#include "pch.h"
#include "CompactionCoordinator.h"

namespace blazeclaw::gateway {

	CompactionResult CompactionCoordinator::TryCompact(
		const std::string& message,
		const std::string& errorCode,
		const std::size_t maxChars) {
		CompactionResult result;
		if (message.empty()) {
			return result;
		}

		const std::string normalizedError = errorCode;
		const bool overflowLike =
			normalizedError.find("overflow") != std::string::npos ||
			normalizedError.find("too_large") != std::string::npos ||
			normalizedError.find("deadline") != std::string::npos;
		if (!overflowLike && message.size() <= maxChars) {
			return result;
		}

		result.applied = true;
		result.reasonCode = "compaction_applied";
		if (message.size() <= maxChars) {
			result.compactedMessage = message;
		}
		else {
			result.compactedMessage =
				message.substr(0, maxChars) + " ...(compacted)";
		}

		return result;
	}

} // namespace blazeclaw::gateway
