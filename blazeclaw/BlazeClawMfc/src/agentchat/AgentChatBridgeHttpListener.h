#pragma once

#include "AgentChatBridgeProtocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace blazeclaw::agentchat {

class AgentChatBridgeHttpListener {
public:
	using RequestHandler = std::function<AgentChatBridgeHttpResponse(
		const std::string& method,
		const std::string& path,
		const std::string& body)>;

	AgentChatBridgeHttpListener();
	~AgentChatBridgeHttpListener();

	bool Start(
		const std::string& bindAddress,
		std::uint16_t port,
		RequestHandler handler);
	void Stop();
	bool IsRunning() const noexcept;
	std::string LastError() const;

private:
	void WorkerLoop();
	void HandleAcceptedSocket(void* acceptedSocketRaw) const;
	static std::string BuildHttpResponse(
		const AgentChatBridgeHttpResponse& response);

	mutable std::mutex m_mutex;
	RequestHandler m_handler;
	std::thread m_worker;
	std::atomic<bool> m_running{ false };
	std::string m_bindAddress = "127.0.0.1";
	std::uint16_t m_port = 0;
	std::string m_lastError;
	void* m_listenSocketRaw = nullptr;
};

} // namespace blazeclaw::agentchat
