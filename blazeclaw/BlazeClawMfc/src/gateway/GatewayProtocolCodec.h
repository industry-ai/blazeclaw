#pragma once

#include "GatewayProtocolJson.h"

namespace blazeclaw::gateway::protocol {

[[nodiscard]] bool TryDecodeRequestFrame(
    const std::string& inboundJson,
    RequestFrame& outFrame,
    std::string& error);

[[nodiscard]] std::string EncodeResponseFrame(const ResponseFrame& frame);
[[nodiscard]] std::string EncodeEventFrame(const EventFrame& frame);

} // namespace blazeclaw::gateway::protocol
