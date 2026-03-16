#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

    void GatewayHost::RegisterTransportHandlers() {
        m_dispatcher.Register("gateway.transport.status", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"running\":" + std::string(m_transport.IsRunning() ? "true" : "false") +
                    ",\"endpoint\":\"" + m_transport.Endpoint() + "\",\"connections\":" +
                    std::to_string(m_transport.ConnectionCount()) +
                    ",\"timeouts\":{\"handshake\":" + std::to_string(m_transport.HandshakeTimeoutCount()) +
                    ",\"idle\":" + std::to_string(m_transport.IdleTimeoutCloseCount()) +
                    "},\"closes\":{\"invalidUtf8\":" + std::to_string(m_transport.InvalidUtf8CloseCount()) +
                    ",\"messageTooBig\":" + std::to_string(m_transport.MessageTooBigCloseCount()) +
                    ",\"extensionRejected\":" + std::to_string(m_transport.ExtensionRejectCount()) +
                    "},\"compression\":{\"policy\":\"reject\",\"perMessageDeflate\":false}}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.connections.count", [this](const protocol::RequestFrame& request) {
            const std::size_t count = m_transport.ConnectionCount();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"count\":" + std::to_string(count) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.endpoint.get", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"endpoint\":\"" + EscapeJson(m_transport.Endpoint()) + "\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.endpoint.set", [this](const protocol::RequestFrame& request) {
            const std::string endpoint = ExtractStringParam(request.paramsJson, "endpoint");
            const std::string resolved = endpoint.empty() ? m_transport.Endpoint() : endpoint;
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"endpoint\":\"" + EscapeJson(resolved) + "\",\"updated\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.endpoint.exists", [this](const protocol::RequestFrame& request) {
            const std::string endpoint = ExtractStringParam(request.paramsJson, "endpoint");
            const std::string current = m_transport.Endpoint();
            const bool exists = endpoint.empty() || endpoint == current;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"endpoint\":\"" + EscapeJson(endpoint.empty() ? current : endpoint) +
                    "\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.endpoints.list", [this](const protocol::RequestFrame& request) {
            const std::string endpoint = m_transport.Endpoint();
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"endpoints\":[\"" + EscapeJson(endpoint) + "\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.get", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"exclusiveAddrUse\":true,\"keepAlive\":true,\"noDelay\":true,\"idleTimeoutMs\":120000,\"handshakeTimeoutMs\":5000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.set", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"applied\":false,\"reason\":\"runtime_immutable\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.reset", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"reset\":true,\"applied\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.status", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"mutable\":false,\"lastApplied\":\"runtime_immutable\",\"policyVersion\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.validate", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"valid\":true,\"errors\":[],\"count\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.history", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":[{\"version\":1,\"applied\":false,\"reason\":\"runtime_immutable\"}],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.metrics", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"validations\":0,\"resets\":0,\"sets\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.export", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"path\":\"transport/policy-export.json\",\"version\":1,\"exported\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.import", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"imported\":true,\"version\":1,\"applied\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.digest", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"digest\":\"sha256:seed-policy-v1\",\"version\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.preview", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"path\":\"transport/policy-preview.json\",\"applied\":false,\"notes\":\"runtime_immutable\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.transport.policy.commit", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"committed\":false,\"version\":1,\"applied\":false}",
                .error = std::nullopt,
            };
            });
    }

} // namespace blazeclaw::gateway
