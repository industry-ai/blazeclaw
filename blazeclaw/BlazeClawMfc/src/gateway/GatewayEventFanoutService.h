#pragma once

#include <string>
#include <optional>
#include <cstdint>

#include "GatewayProtocolCodec.h"
#include "GatewayChatEventPayload.h"

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
		[[nodiscard]] std::string BuildChatEventFrame(
			const std::string& eventPayloadObjectJson,
			std::uint64_t seq) const;

		// NOTE: payload-based overload removed to avoid header/ABI coupling. Callers
		// should serialize normalized payloads via ToWireJson() and use the
		// string-based overload below.
		[[nodiscard]] std::string BuildCronEventFrame(
			const std::string& cronPayloadObjectJson,
			std::uint64_t seq) const;
	};

} // namespace blazeclaw::gateway
