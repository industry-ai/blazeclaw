#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"

namespace blazeclaw::gateway::protocol {
    // Request no-params validation entries
    const std::vector<std::string> noParamsMethods = {
        "gateway.config.session",
        "gateway.transport.policy.session",
        "gateway.events.sessionKey",
        "gateway.models.session",
        "gateway.tools.mapper"
    };
} // namespace blazeclaw::gateway::protocol
