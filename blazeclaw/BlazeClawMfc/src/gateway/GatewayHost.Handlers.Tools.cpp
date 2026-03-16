#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

    void GatewayHost::RegisterToolsHandlers() {
        auto registerFallbackTools =
            [this](const std::string& method, const std::string& counterName) {
                m_dispatcher.Register(method, [this, counterName](const protocol::RequestFrame& request) {
                    const auto tools = m_toolRegistry.List();
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = true,
                        .payloadJson =
                            "{\"" + counterName + "\":0,\"fallback\":0,\"tools\":" +
                            std::to_string(tools.size()) + "}",
                        .error = std::nullopt,
                    };
                    });
            };

        m_dispatcher.Register("gateway.tools.capacity", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            const std::size_t total = tools.size();
            const std::size_t used = 0;
            const std::size_t free = total >= used ? total - used : 0;
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"total\":" + std::to_string(total) +
                    ",\"used\":" + std::to_string(used) +
                    ",\"free\":" + std::to_string(free) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.queue", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            const std::size_t queued = 0;
            const std::size_t running = 0;
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"queued\":" + std::to_string(queued) +
                    ",\"running\":" + std::to_string(running) +
                    ",\"tools\":" + std::to_string(tools.size()) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.scheduler", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"ticks\":0,\"queued\":0,\"tools\":" +
                    std::to_string(tools.size()) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.backlog", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            const std::size_t pending = 0;
            const std::string toolCount = std::to_string(tools.size());
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pending\":" + std::to_string(pending) +
                    ",\"capacity\":" + toolCount +
                    ",\"tools\":" + toolCount + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.window", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowSec\":60,\"calls\":0,\"tools\":" +
                    std::to_string(tools.size()) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.pipeline", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"queued\":0,\"running\":0,\"failed\":0,\"tools\":" +
                    std::to_string(tools.size()) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.tools.dispatch", [this](const protocol::RequestFrame& request) {
            const auto tools = m_toolRegistry.List();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"queued\":0,\"dispatched\":0,\"tools\":" +
                    std::to_string(tools.size()) + "}",
                .error = std::nullopt,
            };
            });

        registerFallbackTools("gateway.tools.router", "routed");
        registerFallbackTools("gateway.tools.selector", "selected");
        registerFallbackTools("gateway.tools.resolver", "resolved");
        registerFallbackTools("gateway.tools.mapper", "mapped");
        registerFallbackTools("gateway.tools.binding", "bound");
        registerFallbackTools("gateway.tools.profile", "profiled");
        registerFallbackTools("gateway.tools.channel", "channelled");
        registerFallbackTools("gateway.tools.route", "routed");
        registerFallbackTools("gateway.tools.account", "accounted");
        registerFallbackTools("gateway.tools.agent", "agented");
        registerFallbackTools("gateway.tools.model", "modelled");
        registerFallbackTools("gateway.tools.config", "configured");
        registerFallbackTools("gateway.tools.policy", "policied");
        registerFallbackTools("gateway.tools.tool", "tooled");
        registerFallbackTools("gateway.tools.transport", "transported");
        registerFallbackTools("gateway.tools.runtime", "runtimed");
        registerFallbackTools("gateway.tools.state", "stated");
        registerFallbackTools("gateway.tools.healthKey", "healthChecked");
        registerFallbackTools("gateway.tools.log", "logged");
        registerFallbackTools("gateway.tools.metric", "metered");
        registerFallbackTools("gateway.tools.trace", "traced");
        registerFallbackTools("gateway.tools.audit", "audited");
        registerFallbackTools("gateway.tools.debug", "debugged");
        registerFallbackTools("gateway.tools.cache", "cached");
        registerFallbackTools("gateway.tools.queueKey", "queuedKey");
        registerFallbackTools("gateway.tools.windowKey", "windowedKey");
        registerFallbackTools("gateway.tools.cursorKey", "cursoredKey");
        registerFallbackTools("gateway.tools.anchorKey", "anchoredKey");
        registerFallbackTools("gateway.tools.offsetKey", "offsettedKey");
        registerFallbackTools("gateway.tools.markerKey", "markeredKey");
        registerFallbackTools("gateway.tools.pointerKey", "pointedKey");
        registerFallbackTools("gateway.tools.tokenKey", "tokenedKey");
        registerFallbackTools("gateway.tools.sequenceKey", "sequencedKey");
        registerFallbackTools("gateway.tools.streamKey", "streamedKey");
        registerFallbackTools("gateway.tools.bundleKey", "bundledKey");
        registerFallbackTools("gateway.tools.packageKey", "packagedKey");
        registerFallbackTools("gateway.tools.archiveKey", "archivedKey");
        registerFallbackTools("gateway.tools.manifestKey", "manifestedKey");
        registerFallbackTools("gateway.tools.profileKey", "profiledKey");
        registerFallbackTools("gateway.tools.templateKey", "templatedKey");
        registerFallbackTools("gateway.tools.revisionKey", "revisionedKey");
        registerFallbackTools("gateway.tools.historyKey", "historiedKey");
        registerFallbackTools("gateway.tools.snapshotKey", "snapshottedKey");
        registerFallbackTools("gateway.tools.indexKey", "indexedKey");
        registerFallbackTools("gateway.tools.windowScopeKey", "windowScopedKey");
        registerFallbackTools("gateway.tools.cursorScopeKey", "cursorScopedKey");
        registerFallbackTools("gateway.tools.anchorScopeKey", "anchorScopedKey");
        registerFallbackTools("gateway.tools.offsetScopeKey", "offsetScopedKey");
        registerFallbackTools("gateway.tools.pointerScopeKey", "pointerScopedKey");
        registerFallbackTools("gateway.tools.tokenScopeKey", "tokenScopedKey");
        registerFallbackTools("gateway.tools.streamScopeKey", "streamScopedKey");
    }

} // namespace blazeclaw::gateway
