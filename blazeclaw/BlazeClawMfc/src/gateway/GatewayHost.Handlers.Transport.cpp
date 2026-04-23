#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostCatalogHelpers.h"

namespace blazeclaw::gateway {

	namespace {
		std::string EscapeJsonLocal(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);

			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		std::string ExtractStringParamLocal(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			const std::string token = "\"" + fieldName + "\"";
			const std::string& params = paramsJson.value();
			const std::size_t keyPos = params.find(token);
			if (keyPos == std::string::npos) {
				return {};
			}

			std::size_t valuePos = params.find(':', keyPos + token.size());
			if (valuePos == std::string::npos) {
				return {};
			}

			valuePos = params.find('"', valuePos);
			if (valuePos == std::string::npos) {
				return {};
			}

			const std::size_t endQuote = params.find('"', valuePos + 1);
			if (endQuote == std::string::npos) {
				return {};
			}

			return params.substr(valuePos + 1, endQuote - valuePos - 1);
		}

		std::string SerializeStringArrayLocal(std::vector<std::string> values) {
			std::sort(values.begin(), values.end());
			values.erase(std::unique(values.begin(), values.end()), values.end());
			std::string json = "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					json += ",";
				}
				json += "\"" + EscapeJsonLocal(values[i]) + "\"";
			}
			json += "]";
			return json;
		}
	}

	void GatewayHost::RegisterTransportHandlers() {
		m_runtimeContext.dispatcher->Register("connect", [this](const protocol::RequestFrame& request) {
			const std::string requestedAgent =
				ExtractStringParamLocal(request.paramsJson, "agent");
			std::vector<std::string> registeredMethods = m_runtimeContext.dispatcher->RegisteredMethods();
			const std::vector<std::string> eventCatalog(
				GatewayEventCatalogNames().begin(),
				GatewayEventCatalogNames().end());
			return protocol::OkResponse(request, "{\"accepted\":true,\"protocol\":3,\"agent\":\"" +
				EscapeJsonLocal(requestedAgent.empty() ? "unknown" : requestedAgent) +
				"\",\"gateway\":\"blazeclaw.gateway.v1\",\"features\":{\"methods\":" +
				SerializeStringArrayLocal(std::move(registeredMethods)) +
				",\"events\":" + SerializeStringArrayLocal(eventCatalog) + "}}");
			});

		m_runtimeContext.dispatcher->Register("last-heartbeat", [this](const protocol::RequestFrame& request) {
			const bool connected = m_runtimeContext.transport->IsRunning();
			return protocol::OkResponse(
				request,
				"{\"ok\":true,\"lastHeartbeatMs\":0,\"connected\":" +
				std::string(connected ? "true" : "false") +
				",\"status\":\"" + std::string(connected ? "connected" : "idle") + "\"}");
			});

		m_runtimeContext.dispatcher->Register("set-heartbeats", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"updated\":true,\"accepted\":true}");
			});

		m_runtimeContext.dispatcher->Register("system-presence", [this](const protocol::RequestFrame& request) {
			const bool running = m_runtimeContext.transport->IsRunning();
			const std::size_t connections = m_runtimeContext.transport->ConnectionCount();
			const std::string hostLabel = "blazeclaw.local";
			const std::string modeLabel = running ? "connected" : "idle";
			const std::string reasonLabel = running
				? "Gateway transport active"
				: "Gateway transport idle";
			const std::string textLabel = "BlazeClaw gateway presence";
			const std::string payload =
				"[{\"instanceId\":\"blazeclaw-mfc-main\",\"host\":\"" +
				EscapeJsonLocal(hostLabel) +
				"\",\"mode\":\"" +
				EscapeJsonLocal(modeLabel) +
				"\",\"version\":\"blazeclaw.mfc\",\"platform\":\"windows\",\"deviceFamily\":\"desktop\",\"modelIdentifier\":\"mfc-webview\",\"lastInputSeconds\":" +
				std::to_string(running ? 0 : 30) +
				",\"reason\":\"" +
				EscapeJsonLocal(reasonLabel) +
				"\",\"text\":\"" +
				EscapeJsonLocal(textLabel) +
				"\",\"ts\":0,\"roles\":[\"operator\"],\"scopes\":[\"operator.read\"],\"connections\":" +
				std::to_string(connections) +
				"}]";
			return protocol::OkResponse(request, payload);
			});

		m_runtimeContext.dispatcher->Register("system-event", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"ok\":true,\"accepted\":true,\"eventId\":\"system-event-1\",\"ts\":0,\"source\":\"gateway.transport\"}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.status", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"running\":" + std::string(m_runtimeContext.transport->IsRunning() ? "true" : "false") +
				",\"endpoint\":\"" + m_runtimeContext.transport->Endpoint() + "\",\"connections\":" +
				std::to_string(m_runtimeContext.transport->ConnectionCount()) +
				",\"timeouts\":{\"handshake\":" + std::to_string(m_runtimeContext.transport->HandshakeTimeoutCount()) +
				",\"idle\":" + std::to_string(m_runtimeContext.transport->IdleTimeoutCloseCount()) +
				"},\"closes\":{\"invalidUtf8\":" + std::to_string(m_runtimeContext.transport->InvalidUtf8CloseCount()) +
				",\"messageTooBig\":" + std::to_string(m_runtimeContext.transport->MessageTooBigCloseCount()) +
				",\"extensionRejected\":" + std::to_string(m_runtimeContext.transport->ExtensionRejectCount()) +
				"},\"auth\":{\"failures\":" + std::to_string(m_runtimeContext.transport->AuthFailureCount()) +
				",\"bearerOk\":" + std::to_string(m_runtimeContext.transport->AuthBearerSuccessCount()) +
				",\"capabilityOk\":" + std::to_string(m_runtimeContext.transport->AuthCapabilitySuccessCount()) +
				"},\"compression\":{\"policy\":\"reject\",\"perMessageDeflate\":false}}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.connections.count", [this](const protocol::RequestFrame& request) {
			const std::size_t count = m_runtimeContext.transport->ConnectionCount();
			return protocol::OkResponse(request, "{\"count\":" + std::to_string(count) + "}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.endpoint.get", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"endpoint\":\"" +
				EscapeJsonLocal(m_runtimeContext.transport->Endpoint()) +
				"\"}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.endpoint.set", [this](const protocol::RequestFrame& request) {
			const std::string endpoint =
				ExtractStringParamLocal(request.paramsJson, "endpoint");
			const std::string resolved = endpoint.empty() ? m_runtimeContext.transport->Endpoint() : endpoint;
			return protocol::OkResponse(request, "{\"endpoint\":\"" +
				EscapeJsonLocal(resolved) +
				"\",\"updated\":false}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.endpoint.exists", [this](const protocol::RequestFrame& request) {
			const std::string endpoint =
				ExtractStringParamLocal(request.paramsJson, "endpoint");
			const std::string current = m_runtimeContext.transport->Endpoint();
			const bool exists = endpoint.empty() || endpoint == current;

			return protocol::OkResponse(request, "{\"endpoint\":\"" +
				EscapeJsonLocal(endpoint.empty() ? current : endpoint) +
				"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.endpoints.list", [this](const protocol::RequestFrame& request) {
			const std::string endpoint = m_runtimeContext.transport->Endpoint();
			return protocol::OkResponse(request, "{\"endpoints\":[\"" +
				EscapeJsonLocal(endpoint) +
				"\"],\"count\":1}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.get", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"exclusiveAddrUse\":true,\"keepAlive\":true,\"noDelay\":true,\"idleTimeoutMs\":120000,\"handshakeTimeoutMs\":5000}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.set", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"applied\":false,\"reason\":\"runtime_immutable\"}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.reset", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"reset\":true,\"applied\":false}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.status", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"mutable\":false,\"lastApplied\":\"runtime_immutable\",\"policyVersion\":1}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.validate", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"valid\":true,\"errors\":[],\"count\":0}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.history", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":[{\"version\":1,\"applied\":false,\"reason\":\"runtime_immutable\"}],\"count\":1}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.metrics", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"validations\":0,\"resets\":0,\"sets\":0}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.export", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"path\":\"transport/policy-export.json\",\"version\":1,\"exported\":true}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.import", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"imported\":true,\"version\":1,\"applied\":false}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.digest", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"digest\":\"sha256:seed-policy-v1\",\"version\":1}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.preview", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"path\":\"transport/policy-preview.json\",\"applied\":false,\"notes\":\"runtime_immutable\"}");
			});

		m_runtimeContext.dispatcher->Register("gateway.transport.policy.commit", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"committed\":false,\"version\":1,\"applied\":false}");
			});
	}

} // namespace blazeclaw::gateway
