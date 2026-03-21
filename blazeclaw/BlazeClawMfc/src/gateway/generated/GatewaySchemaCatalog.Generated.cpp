#include "pch.h"
#include "GatewaySchemaCatalog.Generated.h"

namespace blazeclaw::gateway::generated {
namespace {

constexpr std::array<SchemaMethodRule, 47> kSchemaMethodRules{{
		{ "gateway.ping", "none", "core.pong", "" },
		{ "gateway.protocol.version", "none", "core.protocolVersion", "" },
		{ "gateway.features.list", "none", "catalog.features", "" },
		{ "gateway.events.catalog", "none", "catalog.events", "" },
		{ "gateway.session.list", "objectOptional", "sessions.list", "" },
		{ "gateway.agents.list", "objectOptional", "agents.list", "" },
		{ "gateway.channels.status", "objectOptional", "channels.statusList", "" },
		{ "gateway.channels.accounts", "objectOptional", "channels.accountList", "" },
		{ "gateway.channels.routes", "objectOptional", "channels.routeList", "" },
		{ "gateway.channels.route.resolve", "objectOptional", "channels.routeResolve", "" },
		{ "gateway.tools.call.preview", "objectOptional", "tools.preview", "" },
		{ "gateway.tools.call.execute", "objectOptional", "tools.execute", "" },
		{ "gateway.logs.tail", "objectOptional", "logs.tail", "" },
		{ "gateway.events.list", "none", "events.list", "" },
		{ "gateway.events.get", "objectOptional", "events.get", "event" },
		{ "gateway.events.search", "objectOptional", "events.search", "term" },
		{ "gateway.transport.status", "none", "transport.status", "" },
		{ "gateway.config.get", "none", "config.snapshot", "" },
		{ "gateway.config.set", "objectOptional", "config.setResult", "" },
		{ "gateway.health", "none", "health.summary", "" },
		{ "gateway.health.details", "none", "health.details", "" },
		{ "gateway.logs.levels", "none", "logs.levels", "" },
		{ "gateway.sessions.resolve", "objectOptional", "sessions.resolve", "sessionId" },
		{ "gateway.sessions.exists", "objectOptional", "sessions.exists", "sessionId" },
		{ "gateway.sessions.activate", "objectOptional", "sessions.activate", "sessionId" },
		{ "gateway.sessions.delete", "objectOptional", "sessions.delete", "sessionId" },
		{ "gateway.sessions.usage", "objectOptional", "sessions.usage", "sessionId" },
		{ "gateway.agents.get", "objectOptional", "agents.get", "agentId" },
		{ "gateway.agents.activate", "objectOptional", "agents.activate", "agentId" },
		{ "gateway.agents.delete", "objectOptional", "agents.delete", "agentId" },
		{ "gateway.agents.exists", "objectOptional", "agents.exists", "agentId" },
		{ "gateway.config.exists", "objectOptional", "config.exists", "key" },
		{ "gateway.config.getKey", "objectOptional", "config.getKey", "key" },
		{ "gateway.config.count", "objectOptional", "config.count", "section" },
		{ "gateway.config.getSection", "objectOptional", "config.getSection", "section" },
		{ "gateway.events.exists", "objectOptional", "events.exists", "event" },
		{ "gateway.events.count", "objectOptional", "events.count", "event" },
		{ "gateway.events.latestByType", "objectOptional", "events.latestByType", "type" },
		{ "gateway.models.exists", "objectOptional", "models.exists", "modelId" },
		{ "gateway.models.get", "objectOptional", "models.get", "modelId" },
		{ "gateway.models.count", "objectOptional", "models.count", "provider" },
		{ "gateway.models.listByProvider", "objectOptional", "models.listByProvider", "provider" },
		{ "gateway.tools.exists", "objectOptional", "tools.exists", "tool" },
		{ "gateway.tools.get", "objectOptional", "tools.get", "tool" },
		{ "gateway.tools.list", "objectOptional", "tools.list", "category" },
		{ "gateway.transport.endpoint.exists", "objectOptional", "transport.endpoint.exists", "endpoint" },
		{ "gateway.transport.endpoint.set", "objectOptional", "transport.endpoint.set", "endpoint" },
}};

constexpr std::array<SchemaMethodPatternRule, 13> kSchemaMethodPatternRules{{
		{ "gateway.sessions.*", "none", "sessions.generic" },
		{ "gateway.agents.*", "none", "agents.generic" },
		{ "gateway.channels.*", "none", "channels.generic" },
		{ "gateway.tools.*", "none", "tools.generic" },
		{ "gateway.models.*", "none", "models.generic" },
		{ "gateway.config.*", "none", "config.generic" },
		{ "gateway.transport.*", "none", "transport.generic" },
		{ "gateway.events.*", "none", "events.generic" },
		{ "gateway.runtime.*", "none", "runtime.generic" },
		{ "gateway.security.*", "none", "security.generic" },
		{ "gateway.nodes.*", "none", "nodes.generic" },
		{ "gateway.platform.*", "none", "platform.generic" },
		{ "gateway.ops.*", "none", "ops.generic" },
}};

constexpr std::array<const char*, 8> kSchemaRequiredEvents{{
		"gateway.tick",
		"gateway.health",
		"gateway.shutdown",
		"gateway.channels.update",
		"gateway.channels.accounts.update",
		"gateway.session.reset",
		"gateway.agent.update",
		"gateway.tools.catalog.update",
}};

} // namespace

const std::array<SchemaMethodRule, 47>& GetSchemaMethodRules() noexcept {
    return kSchemaMethodRules;
}

const std::array<SchemaMethodPatternRule, 13>& GetSchemaMethodPatternRules() noexcept {
    return kSchemaMethodPatternRules;
}

const std::array<const char*, 8>& GetSchemaRequiredEvents() noexcept {
    return kSchemaRequiredEvents;
}

} // namespace blazeclaw::gateway::generated
