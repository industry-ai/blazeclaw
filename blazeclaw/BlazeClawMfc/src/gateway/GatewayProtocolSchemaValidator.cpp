#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"

namespace blazeclaw::gateway::protocol {
    // Request no-params validation entries
    const std::vector<std::string> noParamsMethods = {
        "gateway.config.session",
        "gateway.transport.policy.session",
        "gateway.events.sessionKey",
        "gateway.models.session",
        "gateway.tools.mapper",
        "gateway.tools.executions.count",
        "gateway.tools.executions.latest",
        "gateway.tools.executions.clear",
        "gateway.runtime.orchestration.load",
        "gateway.runtime.streaming.buffer",
        "gateway.models.failover.override",
        "gateway.runtime.orchestration.saturation",
        "gateway.runtime.streaming.throttle",
        "gateway.models.failover.override.clear",
        "gateway.runtime.orchestration.pressure",
        "gateway.runtime.streaming.pacing",
        "gateway.models.failover.override.status",
        "gateway.runtime.orchestration.headroom",
        "gateway.runtime.streaming.jitter",
        "gateway.models.failover.override.history",
        "gateway.runtime.orchestration.balance",
        "gateway.runtime.streaming.drift",
        "gateway.models.failover.override.metrics"
    };
} // namespace blazeclaw::gateway::protocol
