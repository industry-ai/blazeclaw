#pragma once

#include "../config/ConfigModels.h"
#include "GatewayAgentRegistry.h"
#include "GatewayChannelRegistry.h"
#include "GatewayMethodDispatcher.h"
#include "GatewaySessionRegistry.h"
#include "GatewayToolRegistry.h"
#include "GatewayWebSocketTransport.h"
#include "GatewayProtocolContract.h"

namespace blazeclaw::gateway {

	class GatewayHost {
	public:
		bool Start(const blazeclaw::config::GatewayConfig& config);
		void Stop();

		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] std::string LastWarning() const;
		[[nodiscard]] std::string HandleInboundText(const std::string& inboundJson) const;
		bool AcceptConnection(const std::string& connectionId, std::string& error);
		bool PumpInboundFrame(const std::string& connectionId, const std::string& inboundFrame, std::string& error);
		std::vector<std::string> DrainOutboundFrames(const std::string& connectionId, std::string& error);
		bool PumpNetworkOnce(std::string& error);
		[[nodiscard]] std::string BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildHealthEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const;
		[[nodiscard]] std::string BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const;
		[[nodiscard]] std::string BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const;

		[[nodiscard]] protocol::ResponseFrame RouteRequest(const protocol::RequestFrame& request) const;

	private:
		void RegisterDefaultHandlers();
      void RegisterChannelsHandlers();
     void RegisterEventHandlers();
     void RegisterToolsHandlers();
     void RegisterScopeClusterHandlers();
     void RegisterGeneratedScopeClusterHandlers();
		void RegisterSecurityOpsHandlers();
		void RegisterRuntimeHandlers();
		void RegisterTransportHandlers();

		bool m_running = false;
		std::string m_bindAddress;
		std::uint16_t m_port = 0;
		std::string m_runtimeGatewayBind = "127.0.0.1";
		std::uint16_t m_runtimeGatewayPort = 18789;
		std::string m_runtimeAgentModel = "default";
		bool m_runtimeAgentStreaming = true;
		std::string m_lastWarning;
		GatewayMethodDispatcher m_dispatcher;
		GatewayWebSocketTransport m_transport;
		GatewayAgentRegistry m_agentRegistry;
		GatewayChannelRegistry m_channelRegistry;
		GatewaySessionRegistry m_sessionRegistry;
		GatewayToolRegistry m_toolRegistry;
	};

} // namespace blazeclaw::gateway
