#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	// canonical in-memory representation for chat events in runtime queues
	struct ChatEventPayload {
		std::string						eventType;		// e.g., "message", "assistant.delta", "lifecycle"
		std::uint64_t					timestampMs = 0;
		std::optional<std::string>		userMessage;
		std::optional<std::string>		assistantDelta;
		std::optional<nlohmann::json>	messageObject;

		[[nodiscard]] std::string ToWireJson() const {
			nlohmann::json obj = nlohmann::json::object();
			obj["timestamp"] = timestampMs;
			obj["type"] = eventType;
			if (messageObject.has_value()) {
				obj["message"] = messageObject.value();
			}
			else if (userMessage.has_value()) {
				obj["message"] = nlohmann::json::object({ {"role","user"},{"text", userMessage.value()} });
			}
			else if (assistantDelta.has_value()) {
				obj["message"] = nlohmann::json::object({ {"role","assistant"},{"text", assistantDelta.value()} });
			}
			return obj.dump();
		}
	};

} // namespace blazeclaw::gateway
