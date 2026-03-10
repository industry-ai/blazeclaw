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
		ChannelAccountEntry GetAccount(const std::string& channel, const std::string& accountId) const;
		ChannelAccountEntry CreateAccount(
			const std::string& channel,
			const std::string& accountId,
			const std::optional<std::string>& label,
			std::optional<bool> active,
			std::optional<bool> connected,
			bool& created);
		ChannelAccountEntry DeleteAccount(const std::string& channel, const std::string& accountId, bool& deleted);
		std::vector<ChannelRouteEntry> ListRoutes() const;
		ChannelRouteEntry ResolveRoute(const std::string& channel, const std::string& accountId) const;
		ChannelLogoutResult Logout(const std::string& channel, const std::string& accountId);
		ChannelAccountEntry ActivateAccount(const std::string& channel, const std::string& accountId, bool& activated);
		ChannelAccountEntry DeactivateAccount(const std::string& channel, const std::string& accountId, bool& deactivated);
		bool AccountExists(const std::string& channel, const std::string& accountId) const;
		ChannelAccountEntry UpdateAccount(
			const std::string& channel,
			const std::string& accountId,
			const std::optional<std::string>& label,
			std::optional<bool> active,
			std::optional<bool> connected,
			bool& updated);
		ChannelRouteEntry SetRoute(
			const std::string& channel,
			const std::string& accountId,
			const std::string& agentId,
			const std::string& sessionId);
		bool DeleteRoute(
			const std::string& channel,
			const std::string& accountId,
			ChannelRouteEntry& removedRoute);
		std::size_t ClearRoutes(const std::string& channel);
		std::size_t RestoreRoutes(const std::string& channel);
		bool RouteExists(const std::string& channel, const std::string& accountId) const;

	private:
		std::vector<ChannelStatusEntry> m_status;
		std::vector<ChannelAccountEntry> m_accounts;
		std::vector<ChannelRouteEntry> m_routes;
	};

} // namespace blazeclaw::gateway
