#include "pch.h"
#include "RuntimeTranscriptGuard.h"

#include "GatewayJsonUtils.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway {
	namespace {
		constexpr char kSilentReplyToken[] = "NO_REPLY";

		std::string ToLower(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}
	}

	std::vector<std::string> RuntimeTranscriptGuard::NormalizeAssistantDeltas(
		const std::vector<std::string>& deltas,
		const std::string& assistantText,
		const bool providerStreamed) {
		std::vector<std::string> normalized = deltas;
		if (normalized.empty() && !assistantText.empty()) {
			normalized.push_back(assistantText);
		}

		if (providerStreamed) {
			normalized.clear();
		}

		return normalized;
	}

	bool RuntimeTranscriptGuard::IsSilentReplyText(const std::string& text) {
		return json::Trim(text) == kSilentReplyToken;
	}

	bool RuntimeTranscriptGuard::IsRetryableErrorCode(
		const std::string& code) {
		const std::string normalized = ToLower(json::Trim(code));
		if (normalized.empty()) {
			return false;
		}

		return normalized.find("timeout") != std::string::npos ||
			normalized.find("queue") != std::string::npos ||
			normalized.find("unavailable") != std::string::npos ||
			normalized.find("throttle") != std::string::npos ||
			normalized.find("rate_limit") != std::string::npos;
	}

	std::optional<std::uint64_t> RuntimeTranscriptGuard::SuggestedRetryAfterMs(
		const std::string& code) {
		const std::string normalized = ToLower(json::Trim(code));
		if (normalized.find("queue") != std::string::npos) {
			return std::uint64_t{ 250 };
		}

		if (normalized.find("timeout") != std::string::npos) {
			return std::uint64_t{ 1000 };
		}

		if (normalized.find("rate_limit") != std::string::npos ||
			normalized.find("throttle") != std::string::npos) {
			return std::uint64_t{ 1500 };
		}

		return std::nullopt;
	}

} // namespace blazeclaw::gateway
