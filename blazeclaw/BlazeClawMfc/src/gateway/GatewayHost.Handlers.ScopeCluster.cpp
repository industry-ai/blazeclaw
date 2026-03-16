#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

    void GatewayHost::RegisterScopeClusterHandlers() {
        auto registerStatic = [this](const std::string& method, const std::string& payload) {
            m_dispatcher.Register(method, [payload](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson = payload,
                    .error = std::nullopt,
                };
                });
            };

        auto registerToolsCount = [this](
            const std::string& method,
            const std::string& fieldName) {
            m_dispatcher.Register(method, [this, fieldName](const protocol::RequestFrame& request) {
                const auto tools = m_toolRegistry.List();
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"" + fieldName + "\":0,\"fallback\":0,\"tools\":" +
                        std::to_string(tools.size()) + "}",
                    .error = std::nullopt,
                };
                });
            };

        const std::vector<std::string> scopeKeyWithTools = {
            "stream",
            "sequence",
            "bundle",
            "package",
            "archive",
            "manifest",
            "profile",
            "template",
            "revision",
            "history",
            "snapshot",
            "index",
            "marker",
        };

        const std::vector<std::string> scopeIdWithTools = {
            "window",
            "cursor",
            "anchor",
            "offset",
            "pointer",
            "token",
            "sequence",
            "stream",
            "bundle",
            "package",
            "archive",
            "manifest",
            "profile",
            "template",
            "revision",
            "history",
            "snapshot",
            "index",
            "marker",
        };

        const std::vector<std::string> modelsConfigScopeKeyOnly = {
            "token",
            "pointer",
            "marker",
            "offset",
            "anchor",
            "cursor",
            "window",
        };

        const std::vector<std::string> modelsConfigSimpleKeys = {
            "index",
            "snapshot",
            "history",
        };

        for (const auto& suffix : scopeKeyWithTools) {
            registerToolsCount(
                "gateway.tools." + suffix + "ScopeKey",
                suffix + "ScopedKey");
        }

        for (const auto& suffix : scopeIdWithTools) {
            registerToolsCount(
                "gateway.tools." + suffix + "ScopeId",
                suffix + "ScopedId");
        }

        for (const auto& suffix : scopeKeyWithTools) {
            registerStatic(
                "gateway.models." + suffix + "ScopeKey",
                "{\"models\":[\"default\",\"reasoner\"],\"count\":2}");
            registerStatic(
                "gateway.config." + suffix + "ScopeKey",
                "{\"sections\":[\"gateway\",\"agent\"],\"count\":2}");
            registerStatic(
                "gateway.transport.policy." + suffix + "ScopeKey",
                "{\"" + suffix + "ScopeScoped\":false,\"version\":1,\"applied\":false}");
        }

        for (const auto& suffix : scopeIdWithTools) {
            registerStatic(
                "gateway.models." + suffix + "ScopeId",
                "{\"models\":[\"default\",\"reasoner\"],\"count\":2}");
            registerStatic(
                "gateway.config." + suffix + "ScopeId",
                "{\"sections\":[\"gateway\",\"agent\"],\"count\":2}");
            registerStatic(
                "gateway.transport.policy." + suffix + "ScopeId",
                "{\"" + suffix + "ScopeScopedId\":false,\"version\":1,\"applied\":false}");
        }

        for (const auto& suffix : modelsConfigScopeKeyOnly) {
            registerStatic(
                "gateway.models." + suffix + "ScopeKey",
                "{\"models\":[\"default\",\"reasoner\"],\"count\":2}");
            registerStatic(
                "gateway.config." + suffix + "ScopeKey",
                "{\"sections\":[\"gateway\",\"agent\"],\"count\":2}");
            registerStatic(
                "gateway.transport.policy." + suffix + "ScopeKey",
                "{\"" + suffix + "ScopeScoped\":false,\"version\":1,\"applied\":false}");
        }

        for (const auto& suffix : modelsConfigSimpleKeys) {
            registerStatic(
                "gateway.models." + suffix + "Key",
                "{\"models\":[\"default\",\"reasoner\"],\"count\":2}");
            registerStatic(
                "gateway.config." + suffix + "Key",
                "{\"sections\":[\"gateway\",\"agent\"],\"count\":2}");
            registerStatic(
                "gateway.transport.policy." + suffix + "Key",
                "{\"" + suffix + "Scoped\":false,\"version\":1,\"applied\":false}");
        }
    }

} // namespace blazeclaw::gateway
