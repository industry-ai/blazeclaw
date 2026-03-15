#pragma once

#include "pch.h"

#include <filesystem>

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

	struct ChannelAdapterDescriptor {
		std::string id;
		std::string label;
		std::string defaultAccountId;
	};

	struct RouteResetResult {
		std::size_t cleared = 0;
		std::size_t restored = 0;
		std::size_t total = 0;
	};

	class GatewayChannelRegistry {
	public:
		GatewayChannelRegistry();
		std::vector<ChannelAdapterDescriptor> ListAdapters() const;

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
		std::size_t ClearAccounts(const std::string& channel);
      std::size_t RestoreAccounts(const std::string& channel);
		std::vector<ChannelRouteEntry> ListRoutes() const;
		ChannelRouteEntry ResolveRoute(const std::string& channel, const std::string& accountId) const;
		ChannelRouteEntry GetRoute(const std::string& channel, const std::string& accountId) const;
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
       ChannelRouteEntry PatchRoute(
			const std::string& channel,
			const std::string& accountId,
			const std::optional<std::string>& agentId,
			const std::optional<std::string>& sessionId,
			bool& updated);
		bool DeleteRoute(
			const std::string& channel,
			const std::string& accountId,
			ChannelRouteEntry& removedRoute);
		ChannelRouteEntry RestoreRoute(const std::string& channel, const std::string& accountId, bool& restored);
		std::size_t ClearRoutes(const std::string& channel);
		std::size_t RestoreRoutes(const std::string& channel);
       RouteResetResult ResetRoutes(const std::string& channel);
		bool RouteExists(const std::string& channel, const std::string& accountId) const;

	private:
       static std::filesystem::path PersistencePath();
		void LoadPersistedState();
		void PersistState() const;
		void RecomputeStatus();

		std::vector<ChannelAdapterDescriptor> m_adapters;
		std::vector<ChannelStatusEntry> m_status;
		std::vector<ChannelAccountEntry> m_accounts;
		std::vector<ChannelRouteEntry> m_routes;
	};

} // namespace blazeclaw::gateway
