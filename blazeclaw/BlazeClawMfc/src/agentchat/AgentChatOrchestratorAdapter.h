#pragma once

#include "../gateway/GatewayProtocolModels.h"

#include <functional>
#include <memory>
#include <optional>

namespace blazeclaw::agentchat {

class IAgentChatOrchestratorAdapter {
public:
	virtual ~IAgentChatOrchestratorAdapter() = default;

	virtual std::optional<blazeclaw::gateway::protocol::ResponseFrame> Route(
		const blazeclaw::gateway::protocol::RequestFrame& request) const = 0;
};

class CallbackAgentChatOrchestratorAdapter final : public IAgentChatOrchestratorAdapter {
public:
	using Router = std::function<blazeclaw::gateway::protocol::ResponseFrame(
		const blazeclaw::gateway::protocol::RequestFrame& request)>;

	CallbackAgentChatOrchestratorAdapter() = default;
	explicit CallbackAgentChatOrchestratorAdapter(Router router)
		: m_router(std::move(router)) {
	}

	void SetRouter(Router router) {
		m_router = std::move(router);
	}

	std::optional<blazeclaw::gateway::protocol::ResponseFrame> Route(
		const blazeclaw::gateway::protocol::RequestFrame& request) const override {
		if (!static_cast<bool>(m_router)) {
			return std::nullopt;
		}
		return m_router(request);
	}

private:
	Router m_router;
};

using AgentChatOrchestratorAdapterPtr = std::shared_ptr<IAgentChatOrchestratorAdapter>;
using AgentChatCallbackOrchestratorAdapterPtr = std::shared_ptr<CallbackAgentChatOrchestratorAdapter>;

} // namespace blazeclaw::agentchat
