#pragma once

#include "AgentChatOrchestratorAdapter.h"
#include "../gateway/GatewayProtocolModels.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace blazeclaw::agentchat {
	class AgentChatNativeRunner;

	namespace test_hooks {
		bool RunnerShouldRespond(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& message,
			const std::string& from,
			const std::string& messageId,
			const std::string& userId,
			const std::string& userPhone);

		std::string RunnerExtractUserText(
			const AgentChatNativeRunner& runner,
			const std::string& raw);

		std::string RunnerBuildReplyText(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& from,
			const std::string& userId,
			const std::string& userPhone,
			const std::string& aiText);

		bool RunnerRouteChatSendAndAwaitFinal(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& from,
			const std::string& userId,
			const std::string& userPhone,
			const std::string& messageId,
			const std::string& userText,
			std::string& responseText,
			std::string& errorOut);
	}

class AgentChatNativeRunner {
public:
	using GatewayRouter = std::function<blazeclaw::gateway::protocol::ResponseFrame(
		const blazeclaw::gateway::protocol::RequestFrame& request)>;

	AgentChatNativeRunner();
	~AgentChatNativeRunner();

	void SetOrchestratorAdapter(AgentChatOrchestratorAdapterPtr adapter);
	void SetGatewayRequestRouter(GatewayRouter router);
	bool Initialize();
	void Shutdown();
	bool IsRunning() const;

private:
	struct IncomingPrivmsg {
		std::string channel;
		std::string message;
		std::string from;
		std::string messageId;
		std::string userId;
		std::string userPhone;
	};

	void WorkerMain();
	bool ConnectAndRunOnce();
	bool ProcessChatMessage(
		void* socketHandle,
		std::uint64_t sessionId,
		std::uint32_t& sequence,
		const IncomingPrivmsg& message,
		std::string& errorOut) const;
	bool ShouldRespond(const IncomingPrivmsg& message) const;
	std::string ExtractUserText(const std::string& raw) const;
	std::string BuildReplyText(const IncomingPrivmsg& message, const std::string& aiText) const;
	bool RouteChatSendAndAwaitFinal(
		const IncomingPrivmsg& message,
		const std::string& userText,
		std::string& responseText,
		std::string& errorOut) const;
	bool SendTypingIndicator(void* socketHandle, std::uint64_t sessionId, const std::string& channel, bool typing) const;
	bool SendReply(
		void* socketHandle,
		std::uint64_t sessionId,
		std::uint32_t& sequence,
		const std::string& channel,
		const std::string& text,
		const std::string& replyToMessageId,
		std::string& errorOut) const;

	mutable std::mutex m_mutex;
	AgentChatOrchestratorAdapterPtr m_orchestratorAdapter;
	std::thread m_worker;
	bool m_running = false;
	std::unordered_set<std::string> m_processingMessageIds;

	friend bool test_hooks::RunnerShouldRespond(
		const AgentChatNativeRunner& runner,
		const std::string& channel,
		const std::string& message,
		const std::string& from,
		const std::string& messageId,
		const std::string& userId,
		const std::string& userPhone);
	friend std::string test_hooks::RunnerExtractUserText(
		const AgentChatNativeRunner& runner,
		const std::string& raw);
	friend std::string test_hooks::RunnerBuildReplyText(
		const AgentChatNativeRunner& runner,
		const std::string& channel,
		const std::string& from,
		const std::string& userId,
		const std::string& userPhone,
		const std::string& aiText);
	friend bool test_hooks::RunnerRouteChatSendAndAwaitFinal(
		const AgentChatNativeRunner& runner,
		const std::string& channel,
		const std::string& from,
		const std::string& userId,
		const std::string& userPhone,
		const std::string& messageId,
		const std::string& userText,
		std::string& responseText,
		std::string& errorOut);
};

} // namespace blazeclaw::agentchat
