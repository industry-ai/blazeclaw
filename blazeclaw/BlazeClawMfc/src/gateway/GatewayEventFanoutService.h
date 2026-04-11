#pragma once

#include "GatewayProtocolCodec.h"

namespace blazeclaw::gateway {

	class GatewayEventFanoutService {
	public:
		struct ChatLifecycleEvent {
			std::string runId;
			std::string sessionKey;
			std::string state;
			std::optional<std::string> messageJson;
			std::optional<std::string> errorMessage;
			std::uint64_t timestampMs = 0;
		};

		[[nodiscard]] std::string BuildChatLifecycleEventFrame(
			const ChatLifecycleEvent& event,
			std::uint64_t seq) const;
	};

} // namespace blazeclaw::gateway
