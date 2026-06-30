#pragma once

#include "AgentChatBridgeConfig.h"
#include "AgentChatBridgeHttpListener.h"
#include "AgentChatOrchestratorAdapter.h"
#include "AgentChatBridgeProtocol.h"
#include "AgentChatBridgeStateStore.h"
#include "../gateway/GatewayProtocolModels.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <functional>

namespace blazeclaw::agentchat {

class AgentChatBridgeHost {
public:
	using GatewayRouter = std::function<blazeclaw::gateway::protocol::ResponseFrame(
		const blazeclaw::gateway::protocol::RequestFrame& request)>;

	AgentChatBridgeHost();
	void SetOrchestratorAdapter(AgentChatOrchestratorAdapterPtr adapter);
	void SetGatewayRequestRouter(GatewayRouter router);

	bool Initialize(const AgentChatBridgeConfig& config);
	void Shutdown();
	bool IsRunning() const;

	AgentChatBridgeHttpResponse HandleRequest(
		const std::string& method,
		const std::string& path,
		const std::string& requestBodyJson) const;

	const AgentChatBridgeConfig& Config() const noexcept;

private:
	std::string ResolveGatewayUrl() const;
	AgentChatBridgeHttpResponse HandleAgentRequest(
		const nlohmann::json& payload) const;
	AgentChatBridgeHttpResponse HandlePushRequest(
		const nlohmann::json& payload) const;
	AgentChatBridgeHttpResponse BuildResponseFromGatewayResponse(
		const blazeclaw::gateway::protocol::ResponseFrame& response) const;
	std::optional<blazeclaw::gateway::protocol::ResponseFrame> RouteGatewayRequest(
		const blazeclaw::gateway::protocol::RequestFrame& request) const;
	std::string ResolvePushToken(
		const nlohmann::json& payload) const;
	std::string ResolveChatHost() const;
	std::uint16_t ResolveChatPort() const;
	std::uint32_t ResolvePushTimeoutMs() const;
	bool SendAgentPushToChatServer(
		const nlohmann::json& payload,
		nlohmann::json& responseOut,
		std::string& errorOut) const;

	mutable std::mutex m_mutex;
	AgentChatBridgeConfig m_config;
	std::optional<AgentChatBridgeStateStore> m_stateStore;
	std::unique_ptr<AgentChatBridgeHttpListener> m_httpListener;
	AgentChatOrchestratorAdapterPtr m_orchestratorAdapter;
	bool m_running = false;
};

} // namespace blazeclaw::agentchat
