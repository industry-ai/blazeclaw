#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway::protocol {

std::string SerializeRequestFrame(const RequestFrame& frame);
std::string SerializeResponseFrame(const ResponseFrame& frame);
std::string SerializeEventFrame(const EventFrame& frame);

} // namespace blazeclaw::gateway::protocol
