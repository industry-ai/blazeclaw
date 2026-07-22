#pragma once

#include "../gateway/GatewayChatEventPayload.h"

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace blazeclaw::agentchat {

	struct AgentChatEventPayload {
		blazeclaw::gateway::ChatEventPayload core;
		std::optional<std::string> requestId;
		std::optional<std::string> runId;
		std::optional<std::string> state;
		std::optional<std::string> error;
		std::optional<std::string> code;
		std::optional<nlohmann::json> extra;

		[[nodiscard]] nlohmann::json ToWireObject() const {
			nlohmann::json wire = nlohmann::json::parse(core.ToWireJson(), nullptr, false);
			if (wire.is_discarded() || !wire.is_object()) {
				wire = nlohmann::json::object();
			}

			if (requestId.has_value()) {
				wire["requestId"] = requestId.value();
			}
			if (runId.has_value()) {
				wire["runId"] = runId.value();
			}
			if (state.has_value()) {
				wire["state"] = state.value();
			}
			if (error.has_value()) {
				wire["error"] = error.value();
			}
			if (code.has_value()) {
				wire["code"] = code.value();
			}

			if (core.assistantDelta.has_value()) {
				wire["text"] = core.assistantDelta.value();
			}
			else if (core.userMessage.has_value()) {
				wire["text"] = core.userMessage.value();
			}

			if (extra.has_value() && extra->is_object()) {
				for (const auto& item : extra->items()) {
					if (!wire.contains(item.key())) {
						wire[item.key()] = item.value();
					}
				}
			}

			return wire;
		}

		[[nodiscard]] std::string ToWireJson() const {
			return ToWireObject().dump(
				-1,
				' ',
				false,
				nlohmann::json::error_handler_t::replace);
		}

		static AgentChatEventPayload FromWireObject(const nlohmann::json& wire) {
			AgentChatEventPayload payload;
			if (!wire.is_object()) {
				return payload;
			}

			payload.extra = wire;
			payload.core.eventType = wire.value("type", std::string());
			payload.core.timestampMs = wire.value("timestamp", static_cast<std::uint64_t>(0));

			const auto requestIdIt = wire.find("requestId");
			if (requestIdIt != wire.end() && requestIdIt->is_string()) {
				payload.requestId = requestIdIt->get<std::string>();
			}
			const auto runIdIt = wire.find("runId");
			if (runIdIt != wire.end() && runIdIt->is_string()) {
				payload.runId = runIdIt->get<std::string>();
			}
			const auto stateIt = wire.find("state");
			if (stateIt != wire.end() && stateIt->is_string()) {
				payload.state = stateIt->get<std::string>();
			}
			const auto errorIt = wire.find("error");
			if (errorIt != wire.end() && errorIt->is_string()) {
				payload.error = errorIt->get<std::string>();
			}
			const auto codeIt = wire.find("code");
			if (codeIt != wire.end() && codeIt->is_string()) {
				payload.code = codeIt->get<std::string>();
			}

			const auto textIt = wire.find("text");
			if (textIt != wire.end() && textIt->is_string()) {
				const std::string text = textIt->get<std::string>();
				if (payload.core.eventType == "user" || payload.core.eventType == "message") {
					payload.core.userMessage = text;
				}
				else {
					payload.core.assistantDelta = text;
				}
			}

			const auto messageIt = wire.find("message");
			if (messageIt != wire.end()) {
				payload.core.messageObject = *messageIt;
			}

			return payload;
		}
	};

} // namespace blazeclaw::agentchat
