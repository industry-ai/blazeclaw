#include "pch.h"
#include "GatewaySchemaCatalog.Generated.h"

namespace blazeclaw::gateway::generated {
namespace {

constexpr std::array<SchemaMethodRule, 19> kSchemaMethodRules{{
		{ "gateway.ping", "none", "core.pong" },
		{ "gateway.protocol.version", "none", "core.protocolVersion" },
		{ "gateway.features.list", "none", "catalog.features" },
		{ "gateway.events.catalog", "none", "catalog.events" },
		{ "gateway.session.list", "objectOptional", "sessions.list" },
		{ "gateway.agents.list", "objectOptional", "agents.list" },
		{ "gateway.channels.status", "objectOptional", "channels.statusList" },
		{ "gateway.channels.accounts", "objectOptional", "channels.accountList" },
		{ "gateway.channels.routes", "objectOptional", "channels.routeList" },
		{ "gateway.channels.route.resolve", "objectOptional", "channels.routeResolve" },
		{ "gateway.tools.call.preview", "objectOptional", "tools.preview" },
		{ "gateway.tools.call.execute", "objectOptional", "tools.execute" },
		{ "gateway.logs.tail", "objectOptional", "logs.tail" },
		{ "gateway.events.list", "none", "events.list" },
		{ "gateway.events.get", "objectOptional", "events.get" },
		{ "gateway.events.search", "objectOptional", "events.search" },
		{ "gateway.transport.status", "none", "transport.status" },
		{ "gateway.config.get", "none", "config.snapshot" },
		{ "gateway.config.set", "objectOptional", "config.setResult" },
}};

constexpr std::array<SchemaMethodPatternRule, 13> kSchemaMethodPatternRules{{
		{ "gateway.sessions.*", "objectOptional", "sessions.generic" },
		{ "gateway.agents.*", "objectOptional", "agents.generic" },
		{ "gateway.channels.*", "objectOptional", "channels.generic" },
		{ "gateway.tools.*", "objectOptional", "tools.generic" },
		{ "gateway.models.*", "objectOptional", "models.generic" },
		{ "gateway.config.*", "objectOptional", "config.generic" },
		{ "gateway.transport.*", "objectOptional", "transport.generic" },
		{ "gateway.events.*", "objectOptional", "events.generic" },
		{ "gateway.runtime.*", "objectOptional", "runtime.generic" },
		{ "gateway.security.*", "none", "security.generic" },
		{ "gateway.nodes.*", "none", "nodes.generic" },
		{ "gateway.platform.*", "none", "platform.generic" },
		{ "gateway.ops.*", "none", "ops.generic" },
}};

} // namespace

const std::array<SchemaMethodRule, 19>& GetSchemaMethodRules() noexcept {
    return kSchemaMethodRules;
}

const std::array<SchemaMethodPatternRule, 13>& GetSchemaMethodPatternRules() noexcept {
    return kSchemaMethodPatternRules;
}

} // namespace blazeclaw::gateway::generated
