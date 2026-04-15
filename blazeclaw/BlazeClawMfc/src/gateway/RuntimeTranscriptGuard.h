#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	class RuntimeTranscriptGuard {
	public:
		[[nodiscard]] static std::vector<std::string> NormalizeAssistantDeltas(
			const std::vector<std::string>& deltas,
			const std::string& assistantText,
			bool providerStreamed);

		[[nodiscard]] static bool IsSilentReplyText(const std::string& text);

		[[nodiscard]] static bool IsRetryableErrorCode(
			const std::string& code);

		[[nodiscard]] static std::optional<std::uint64_t> SuggestedRetryAfterMs(
			const std::string& code);
	};

} // namespace blazeclaw::gateway
