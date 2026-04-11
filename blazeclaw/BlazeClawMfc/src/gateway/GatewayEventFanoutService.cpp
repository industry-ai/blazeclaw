#include "pch.h"
#include "GatewayEventFanoutService.h"

#include "GatewayProtocolSchemaValidator.h"

namespace blazeclaw::gateway {

	namespace {
		std::string EscapeJson(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);

			for (const char ch : value) {
				switch (ch) {
				case '\\':
					escaped += "\\\\";
					break;
				case '"':
					escaped += "\\\"";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped += ch;
					break;
				}
			}

			return escaped;
		}

		std::string BuildLifecyclePayload(
			const GatewayEventFanoutService::ChatLifecycleEvent& event) {
			std::string payload =
				"{\"runId\":\"" +
				EscapeJson(event.runId) +
				"\",\"sessionKey\":\"" +
				EscapeJson(event.sessionKey) +
				"\",\"state\":\"" +
				EscapeJson(event.state) +
				"\",\"timestamp\":" +
				std::to_string(event.timestampMs);

			if (event.messageJson.has_value()) {
				payload += ",\"message\":" + event.messageJson.value();
			}

			if (event.errorMessage.has_value()) {
				payload +=
					",\"errorMessage\":\"" +
					EscapeJson(event.errorMessage.value()) +
					"\"";
			}

			payload += "}";
			return payload;
		}
	}

	std::string GatewayEventFanoutService::BuildChatLifecycleEventFrame(
		const ChatLifecycleEvent& event,
		const std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "chat.lifecycle",
			.payloadJson = BuildLifecyclePayload(event),
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"chat.lifecycle\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

} // namespace blazeclaw::gateway
