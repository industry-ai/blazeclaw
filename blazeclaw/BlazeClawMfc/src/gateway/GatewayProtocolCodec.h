#pragma once

#include <string>

#include "GatewayProtocolJson.h"

namespace blazeclaw::gateway::protocol {

[[nodiscard]] bool TryDecodeRequestFrame(
    const std::string& inboundJson,
    RequestFrame& outFrame,
    std::string& error);

[[nodiscard]] std::string EncodeResponseFrame(const ResponseFrame& frame);
[[nodiscard]] std::string EncodeEventFrame(const EventFrame& frame);

/// Build an event frame, run schema validation, and on failure emit `gateway.schema.error` with the given `validationStage` in the payload before encoding.
[[nodiscard]] std::string EncodeValidatedEvent(
	std::string eventName,
	std::string payloadJson,
	std::uint64_t seq,
	const std::string& validationStage);

} // namespace blazeclaw::gateway::protocol
