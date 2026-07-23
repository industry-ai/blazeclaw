#pragma once

#include "../../gateway/GatewayChatEventPayload.h"

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace blazeclaw::corechat {

	struct CoreChatEvent {
		blazeclaw::gateway::ChatEventPayload inner;
		std::optional<std::string> runId;
		std::optional<std::string> requestId;
		std::optional<std::string> sessionId;
		std::optional<std::string> state;
		std::optional<nlohmann::json> extra;

		[[nodiscard]] nlohmann::json ToWireObject() const {
			nlohmann::json obj = nlohmann::json::parse(inner.ToWireJson(), nullptr, false);
			if (obj.is_discarded() || !obj.is_object()) {
				obj = nlohmann::json::object();
			}

			if (runId.has_value()) {
				obj["runId"] = runId.value();
			}
			if (requestId.has_value()) {
				obj["requestId"] = requestId.value();
			}
			if (sessionId.has_value()) {
				obj["sessionId"] = sessionId.value();
			}
			if (state.has_value()) {
				obj["state"] = state.value();
			}

			if (extra.has_value() && extra->is_object()) {
				for (const auto& item : extra->items()) {
					if (!obj.contains(item.key())) {
						obj[item.key()] = item.value();
					}
				}
			}

			return obj;
		}

		[[nodiscard]] std::string ToWireJson() const {
			return ToWireObject().dump(
				-1,
				' ',
				false,
				nlohmann::json::error_handler_t::replace);
		}

		[[nodiscard]] static CoreChatEvent FromWireObject(const nlohmann::json& wire) {
			CoreChatEvent event;
			if (!wire.is_object()) {
				return event;
			}

			event.extra = wire;
			event.inner.eventType = wire.value("type", std::string());
			event.inner.timestampMs = wire.value("timestamp", static_cast<std::uint64_t>(0));
			if (event.inner.timestampMs == 0) {
				event.inner.timestampMs = wire.value("timestampMs", static_cast<std::uint64_t>(0));
			}

			if (const auto runIdIt = wire.find("runId");
				runIdIt != wire.end() && runIdIt->is_string()) {
				event.runId = runIdIt->get<std::string>();
			}
			if (const auto requestIdIt = wire.find("requestId");
				requestIdIt != wire.end() && requestIdIt->is_string()) {
				event.requestId = requestIdIt->get<std::string>();
			}
			if (const auto sessionIdIt = wire.find("sessionId");
				sessionIdIt != wire.end() && sessionIdIt->is_string()) {
				event.sessionId = sessionIdIt->get<std::string>();
			}
			if (const auto stateIt = wire.find("state");
				stateIt != wire.end() && stateIt->is_string()) {
				event.state = stateIt->get<std::string>();
			}

			if (const auto messageIt = wire.find("message"); messageIt != wire.end()) {
				event.inner.messageObject = *messageIt;
			}
			else if (const auto textIt = wire.find("text");
				textIt != wire.end() && textIt->is_string()) {
				event.inner.assistantDelta = textIt->get<std::string>();
			}

			return event;
		}
	};

} // namespace blazeclaw::corechat
