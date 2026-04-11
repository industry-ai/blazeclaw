#pragma once

#include <string>

namespace blazeclaw::gateway {

	class ChatTranscriptStore {
	public:
		struct AppendParams {
			std::string sessionKey;
			std::string message;
			std::string label;
			std::string idempotencyKey;
		};

		struct AppendResult {
			bool ok = false;
			std::string messageId;
			std::string messageJson;
			std::string error;
		};

		[[nodiscard]] AppendResult AppendAssistantMessage(
			const AppendParams& params) const;
	};

} // namespace blazeclaw::gateway
