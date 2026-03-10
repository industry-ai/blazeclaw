#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

	struct ChannelStatusEntry {
		std::string id;
		std::string label;
		bool connected = false;
		std::size_t accountCount = 0;
	};

	struct ChannelAccountEntry {
		std::string channel;
		std::string accountId;
		std::string label;
		bool active = false;
		bool connected = false;
	};

	struct ChannelRouteEntry {
		std::string channel;
		std::string accountId;
		std::string agentId;
		std::string sessionId;
	};

	struct ChannelLogoutResult {
		bool loggedOut = false;
		std::size_t affected = 0;
	};

	class GatewayChannelRegistry {
	public:
		GatewayChannelRegistry();

		std::vector<ChannelStatusEntry> ListStatus() const;
		std::vector<ChannelAccountEntry> ListAccounts() const;
		std::vector<ChannelRouteEntry> ListRoutes() const;
		ChannelRouteEntry ResolveRoute(const std::string& channel, const std::string& accountId) const;
		ChannelLogoutResult Logout(const std::string& channel, const std::string& accountId);

	private:
		std::vector<ChannelStatusEntry> m_status;
		std::vector<ChannelAccountEntry> m_accounts;
		std::vector<ChannelRouteEntry> m_routes;
	};

} // namespace blazeclaw::gateway
