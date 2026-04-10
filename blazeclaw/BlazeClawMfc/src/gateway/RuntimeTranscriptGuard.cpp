#include "pch.h"
#include "RuntimeTranscriptGuard.h"

#include "GatewayJsonUtils.h"

namespace blazeclaw::gateway {
	namespace {
		constexpr char kSilentReplyToken[] = "NO_REPLY";
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

} // namespace blazeclaw::gateway
