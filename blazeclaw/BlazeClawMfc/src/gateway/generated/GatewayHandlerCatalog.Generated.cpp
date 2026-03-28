#include "pch.h"
#include "../GatewayHost.h"

#include <array>
#include <algorithm>
#include <string_view>

namespace blazeclaw::gateway {
    namespace {
        constexpr std::array<std::string_view, 196> kGeneratedMethodCatalog = {
		"gateway.agents.list",
		"gateway.config.anchorScopeId",
		"gateway.config.anchorScopeKey",
		"gateway.config.archiveScopeId",
		"gateway.config.archiveScopeKey",
		"gateway.config.audit",
		"gateway.config.backup",
		"gateway.config.bundleScopeId",
		"gateway.config.bundleScopeKey",
		"gateway.config.cursorScopeId",
		"gateway.config.cursorScopeKey",
		"gateway.config.diff",
		"gateway.config.history",
		"gateway.config.historyKey",
		"gateway.config.historyScopeId",
		"gateway.config.historyScopeKey",
		"gateway.config.indexKey",
		"gateway.config.indexScopeId",
		"gateway.config.indexScopeKey",
		"gateway.config.keys",
		"gateway.config.manifestScopeId",
		"gateway.config.manifestScopeKey",
		"gateway.config.markerScopeId",
		"gateway.config.markerScopeKey",
		"gateway.config.offsetScopeId",
		"gateway.config.offsetScopeKey",
		"gateway.config.packageScopeId",
		"gateway.config.packageScopeKey",
		"gateway.config.pointerScopeId",
		"gateway.config.pointerScopeKey",
		"gateway.config.profileScopeId",
		"gateway.config.profileScopeKey",
		"gateway.config.revision",
		"gateway.config.revisionScopeId",
		"gateway.config.revisionScopeKey",
		"gateway.config.rollback",
		"gateway.config.schema",
		"gateway.config.sections",
		"gateway.config.sequenceScopeId",
		"gateway.config.sequenceScopeKey",
		"gateway.config.snapshotKey",
		"gateway.config.snapshotScopeId",
		"gateway.config.snapshotScopeKey",
		"gateway.config.streamScopeId",
		"gateway.config.streamScopeKey",
		"gateway.config.templateScopeId",
		"gateway.config.templateScopeKey",
		"gateway.config.tokenScopeId",
		"gateway.config.tokenScopeKey",
		"gateway.config.validate",
		"gateway.config.windowScopeId",
		"gateway.config.windowScopeKey",
		"gateway.events.channels",
		"gateway.events.sample",
		"gateway.events.timeline",
		"gateway.events.types",
		"gateway.events.window",
		"gateway.models.affinity",
		"gateway.models.anchorScopeId",
		"gateway.models.anchorScopeKey",
		"gateway.models.archiveScopeId",
		"gateway.models.archiveScopeKey",
		"gateway.models.bundleScopeId",
		"gateway.models.bundleScopeKey",
		"gateway.models.catalog",
		"gateway.models.compatibility",
		"gateway.models.cursorScopeId",
		"gateway.models.cursorScopeKey",
		"gateway.models.default.get",
		"gateway.models.fallback",
		"gateway.models.historyKey",
		"gateway.models.historyScopeId",
		"gateway.models.historyScopeKey",
		"gateway.models.indexKey",
		"gateway.models.indexScopeId",
		"gateway.models.indexScopeKey",
		"gateway.models.manifest",
		"gateway.models.manifestScopeId",
		"gateway.models.manifestScopeKey",
		"gateway.models.markerScopeId",
		"gateway.models.markerScopeKey",
		"gateway.models.offsetScopeId",
		"gateway.models.offsetScopeKey",
		"gateway.models.packageScopeId",
		"gateway.models.packageScopeKey",
		"gateway.models.pointerScopeId",
		"gateway.models.pointerScopeKey",
		"gateway.models.pool",
		"gateway.models.preference",
		"gateway.models.priority",
		"gateway.models.profileScopeId",
		"gateway.models.profileScopeKey",
		"gateway.models.providers",
		"gateway.models.recommended",
		"gateway.models.revisionScopeId",
		"gateway.models.revisionScopeKey",
		"gateway.models.routing",
		"gateway.models.selection",
		"gateway.models.sequenceScopeId",
		"gateway.models.sequenceScopeKey",
		"gateway.models.snapshotKey",
		"gateway.models.snapshotScopeId",
		"gateway.models.snapshotScopeKey",
		"gateway.models.streamScopeId",
		"gateway.models.streamScopeKey",
		"gateway.models.templateScopeId",
		"gateway.models.templateScopeKey",
		"gateway.models.tokenScopeId",
		"gateway.models.tokenScopeKey",
		"gateway.models.windowScopeId",
		"gateway.models.windowScopeKey",
		"gateway.ping",
		"gateway.protocol.version",
		"gateway.sessions.count",
		"gateway.tools.anchorScopeId",
		"gateway.tools.archiveScopeId",
		"gateway.tools.archiveScopeKey",
		"gateway.tools.bundleScopeId",
		"gateway.tools.bundleScopeKey",
		"gateway.tools.call.execute",
		"gateway.tools.cursorScopeId",
		"gateway.tools.errors",
		"gateway.tools.failures",
		"gateway.tools.health",
		"gateway.tools.historyScopeId",
		"gateway.tools.historyScopeKey",
		"gateway.tools.indexScopeId",
		"gateway.tools.indexScopeKey",
		"gateway.tools.latency",
		"gateway.tools.manifestScopeId",
		"gateway.tools.manifestScopeKey",
		"gateway.tools.markerScopeId",
		"gateway.tools.markerScopeKey",
		"gateway.tools.metrics",
		"gateway.tools.offsetScopeId",
		"gateway.tools.packageScopeId",
		"gateway.tools.packageScopeKey",
		"gateway.tools.pointerScopeId",
		"gateway.tools.profileScopeId",
		"gateway.tools.profileScopeKey",
		"gateway.tools.revisionScopeId",
		"gateway.tools.revisionScopeKey",
		"gateway.tools.sequenceScopeId",
		"gateway.tools.sequenceScopeKey",
		"gateway.tools.snapshotScopeId",
		"gateway.tools.snapshotScopeKey",
		"gateway.tools.stats",
		"gateway.tools.streamScopeId",
		"gateway.tools.streamScopeKey",
		"gateway.tools.templateScopeId",
		"gateway.tools.templateScopeKey",
		"gateway.tools.throughput",
		"gateway.tools.tokenScopeId",
		"gateway.tools.usage",
		"gateway.tools.windowScopeId",
		"gateway.transport.policy.anchorScopeId",
		"gateway.transport.policy.anchorScopeKey",
		"gateway.transport.policy.archiveScopeId",
		"gateway.transport.policy.archiveScopeKey",
		"gateway.transport.policy.bundleScopeId",
		"gateway.transport.policy.bundleScopeKey",
		"gateway.transport.policy.cursorScopeId",
		"gateway.transport.policy.cursorScopeKey",
		"gateway.transport.policy.historyKey",
		"gateway.transport.policy.historyScopeId",
		"gateway.transport.policy.historyScopeKey",
		"gateway.transport.policy.indexKey",
		"gateway.transport.policy.indexScopeId",
		"gateway.transport.policy.indexScopeKey",
		"gateway.transport.policy.manifestScopeId",
		"gateway.transport.policy.manifestScopeKey",
		"gateway.transport.policy.markerScopeId",
		"gateway.transport.policy.markerScopeKey",
		"gateway.transport.policy.offsetScopeId",
		"gateway.transport.policy.offsetScopeKey",
		"gateway.transport.policy.packageScopeId",
		"gateway.transport.policy.packageScopeKey",
		"gateway.transport.policy.pointerScopeId",
		"gateway.transport.policy.pointerScopeKey",
		"gateway.transport.policy.profileScopeId",
		"gateway.transport.policy.profileScopeKey",
		"gateway.transport.policy.revisionScopeId",
		"gateway.transport.policy.revisionScopeKey",
		"gateway.transport.policy.sequenceScopeId",
		"gateway.transport.policy.sequenceScopeKey",
		"gateway.transport.policy.snapshotKey",
		"gateway.transport.policy.snapshotScopeId",
		"gateway.transport.policy.snapshotScopeKey",
		"gateway.transport.policy.streamScopeId",
		"gateway.transport.policy.streamScopeKey",
		"gateway.transport.policy.templateScopeId",
		"gateway.transport.policy.templateScopeKey",
		"gateway.transport.policy.tokenScopeId",
		"gateway.transport.policy.tokenScopeKey",
		"gateway.transport.policy.windowScopeId",
		"gateway.transport.policy.windowScopeKey",
        };
    }

    void GatewayHost::RegisterGeneratedScopeClusterHandlers() {
        (void)kGeneratedMethodCatalog;

		m_dispatcher.Register("gateway.ping", [payload = std::string("{\"pong\":true}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.protocol.version", [payload = std::string("{\"minProtocol\":1,\"maxProtocol\":1}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.keys", [payload = std::string("{\"keys\":[\"gateway.bind\",\"gateway.port\",\"agent.model\",\"agent.streaming\"],\"count\":4}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.providers", [payload = std::string("{\"providers\":[\"seed\",\"deepseek\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.window", [payload = std::string("{\"events\":[\"gateway.session.reset\",\"gateway.agent.update\",\"gateway.tools.catalog.update\"],\"count\":3}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.affinity", [payload = std::string("{\"models\":[\"default\",\"reasoner\"],\"affinity\":\"balanced\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.pool", [payload = std::string("{\"models\":[\"default\",\"reasoner\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.manifest", [payload = std::string("{\"models\":[\"default\",\"reasoner\"],\"manifestVersion\":1}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.catalog", [payload = std::string("{\"models\":[\"default\",\"reasoner\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.history", [payload = std::string("{\"revisions\":[1],\"count\":1}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.sample", [payload = std::string("{\"events\":[\"gateway.health\",\"gateway.tools.catalog.update\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.priority", [payload = std::string("{\"models\":[\"default\",\"reasoner\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.revision", [payload = std::string("{\"revision\":1,\"source\":\"runtime\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.preference", [payload = std::string("{\"model\":\"default\",\"provider\":\"seed\",\"source\":\"runtime\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.channels", [payload = std::string("{\"channelEvents\":3,\"accountEvents\":1,\"count\":4}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.routing", [payload = std::string("{\"primary\":\"default\",\"fallback\":\"reasoner\",\"strategy\":\"seed_priority\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.diff", [payload = std::string("{\"changed\":[],\"count\":0}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.timeline", [payload = std::string("{\"events\":[\"gateway.shutdown\",\"gateway.session.reset\",\"gateway.agent.update\",\"gateway.tools.catalog.update\"],\"count\":4}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.selection", [payload = std::string("{\"selected\":\"default\",\"strategy\":\"seed_priority\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.types", [payload = std::string("{\"types\":[\"lifecycle\",\"update\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.backup", [payload = std::string("{\"saved\":true,\"version\":1,\"path\":\"config/runtime.backup.json\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.fallback", [payload = std::string("{\"preferred\":\"default\",\"fallback\":\"reasoner\",\"configured\":true}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.rollback", [payload = std::string("{\"rolledBack\":false,\"version\":1}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.recommended", [payload = std::string("{\"model\":{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true},\"reason\":\"seed_default\"}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.audit", [payload = std::string("{\"enabled\":true,\"source\":\"runtime\",\"lastUpdatedMs\":1735689600000}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.compatibility", [payload = std::string("{\"default\":\"full\",\"reasoner\":\"partial\",\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.validate", [payload = std::string("{\"valid\":true,\"errors\":[],\"count\":0}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.default.get", [payload = std::string("{\"model\":{\"id\":\"default\",\"provider\":\"seed\",\"displayName\":\"Default Model\",\"streaming\":true}}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.schema", [payload = std::string("{\"gateway\":{\"bind\":\"string\",\"port\":\"number\"},\"agent\":{\"model\":\"string\",\"streaming\":\"boolean\"}}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.sections", [payload = std::string("{\"sections\":[\"gateway\",\"agent\"],\"count\":2}")](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});


		m_dispatcher.Register("gateway.tools.throughput", [this, templateJson = std::string("{\"calls\":0,\"windowSec\":60,\"perMinute\":0,\"tools\":{toolsCount}}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.errors", [this, templateJson = std::string("{\"errors\":0,\"tools\":{toolsCount},\"rate\":0}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.latency", [this, templateJson = std::string("{\"minMs\":0,\"maxMs\":0,\"avgMs\":0,\"samples\":{toolsCount}}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.usage", [this, templateJson = std::string("{\"calls\":0,\"tools\":{toolsCount},\"avgMs\":0}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.failures", [this, templateJson = std::string("{\"failed\":0,\"total\":{toolsCount},\"rate\":0}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.metrics", [this, templateJson = std::string("{\"invocations\":0,\"enabled\":{enabledCount},\"disabled\":{disabledCount},\"total\":{toolsCount}}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.stats", [this, templateJson = std::string("{\"enabled\":{enabledCount},\"disabled\":{disabledCount},\"total\":{toolsCount}}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.health", [this, templateJson = std::string("{\"healthy\":{healthyBool},\"enabled\":{enabledCount},\"total\":{toolsCount}}")](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			const std::size_t toolsCount = tools.size();
			const std::size_t enabledCount = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [](const ToolCatalogEntry& item) {
				return item.enabled;
			}));
			const std::size_t disabledCount = toolsCount >= enabledCount ? toolsCount - enabledCount : 0;
			const bool healthy = enabledCount == toolsCount;
			std::string payload = templateJson;
			auto ReplaceToken = [&](const std::string& token, const std::string& value) {
				std::size_t pos = 0;
				while ((pos = payload.find(token, pos)) != std::string::npos) {
					payload.replace(pos, token.size(), value);
					pos += value.size();
				}
			};
			ReplaceToken("{toolsCount}", std::to_string(toolsCount));
			ReplaceToken("{enabledCount}", std::to_string(enabledCount));
			ReplaceToken("{disabledCount}", std::to_string(disabledCount));
			ReplaceToken("{healthyBool}", healthy ? "true" : "false" );
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});


        RegisterScopeClusterHandlers();
    }

} // namespace blazeclaw::gateway
