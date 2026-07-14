#include "pch.h"
#include "GatewayEventFanoutService.h"
// Keep the full payload definition available in this implementation TU.
#include "GatewayChatEventPayload.h"
#include "GatewayJsonUtils.h"
#include "GatewayProtocolCodec.h"

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
		return protocol::EncodeValidatedEvent(
			"chat.lifecycle",
			BuildLifecyclePayload(event),
			seq,
			"chat.lifecycle");
	}

	std::string GatewayEventFanoutService::BuildChatEventFrame(
		const std::string& eventPayloadObjectJson,
		const std::uint64_t seq) const {
		const std::string trimmed = json::Trim(eventPayloadObjectJson);
		const std::string payload =
			trimmed.empty() || trimmed.front() != '{'
			? std::string("{}")
			: trimmed;
		return protocol::EncodeValidatedEvent(
			"chat",
			payload,
			seq,
			"chat");
	}

// The payload-typed overload was intentionally removed from the header to
// reduce header coupling. Keep no implementation here.

	std::string GatewayEventFanoutService::BuildCronEventFrame(
		const std::string& cronPayloadObjectJson,
		const std::uint64_t seq) const {
		const std::string trimmed = json::Trim(cronPayloadObjectJson);
		const std::string payload =
			trimmed.empty() || trimmed.front() != '{'
			? std::string("{}")
			: trimmed;
		return protocol::EncodeValidatedEvent(
			"cron",
			payload,
			seq,
			"cron");
	}

} // namespace blazeclaw::gateway
