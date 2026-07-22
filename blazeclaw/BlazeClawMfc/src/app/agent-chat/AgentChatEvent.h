#pragma once

#include "../../gateway/GatewayChatEventPayload.h" // defines ChatEventPayload
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace blazeclaw::agentchat {

	struct AgentChatEvent {
		// inner canonical message (user/assistant/delta)
		blazeclaw::gateway::ChatEventPayload inner;

		// agent-specific metadata
		std::optional<std::string> requestId;
		std::optional<std::string> runId;
		std::optional<std::string> promptRunId;
		std::optional<std::string> state; // e.g., "final", "partial"
		std::optional<nlohmann::json> extra; // toolCalls, approval, etc.

		[[nodiscard]] nlohmann::json ToWireObject() const {
			nlohmann::json obj = nlohmann::json::parse(inner.ToWireJson(), nullptr, false);
			if (obj.is_discarded() || !obj.is_object()) {
				obj = nlohmann::json::object();
			}

			if (requestId.has_value()) {
				obj["requestId"] = requestId.value();
			}
			if (runId.has_value()) {
				obj["runId"] = runId.value();
			}
			if (promptRunId.has_value()) {
				obj["promptRunId"] = promptRunId.value();
			}
			if (state.has_value()) {
				obj["state"] = state.value();
			}
			if (extra.has_value()) {
				obj["extra"] = extra.value();
			}

			return obj;
		}

		// Produce the wire JSON expected by the frontend / SSE consumers
		[[nodiscard]] std::string ToWireJson() const {
			return ToWireObject().dump(
				-1,
				' ',
				false,
				nlohmann::json::error_handler_t::replace);
		}
	};

} // namespace blazeclaw::agentchat