#include "pch.h"
#include "GatewayChannelRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

	GatewayChannelRegistry::GatewayChannelRegistry() {
		m_status = {
			ChannelStatusEntry{.id = "telegram", .label = "Telegram", .connected = false, .accountCount = 1},
			ChannelStatusEntry{.id = "discord", .label = "Discord", .connected = false, .accountCount = 1},
		};

		m_accounts = {
			ChannelAccountEntry{.channel = "telegram", .accountId = "telegram.default", .label = "Telegram Default", .active = true, .connected = false},
			ChannelAccountEntry{.channel = "discord", .accountId = "discord.default", .label = "Discord Default", .active = true, .connected = false},
		};

		m_routes = {
			ChannelRouteEntry{.channel = "telegram", .accountId = "telegram.default", .agentId = "default", .sessionId = "main"},
			ChannelRouteEntry{.channel = "discord", .accountId = "discord.default", .agentId = "default", .sessionId = "main"},
		};
	}

	std::vector<ChannelStatusEntry> GatewayChannelRegistry::ListStatus() const {
		return m_status;
	}

	std::vector<ChannelAccountEntry> GatewayChannelRegistry::ListAccounts() const {
		return m_accounts;
	}

	std::vector<ChannelRouteEntry> GatewayChannelRegistry::ListRoutes() const {
		return m_routes;
	}

	ChannelRouteEntry GatewayChannelRegistry::ResolveRoute(
		const std::string& channel,
		const std::string& accountId) const {
		const auto routeIt = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& entry) {
			const bool channelMatches = channel.empty() || entry.channel == channel;
			const bool accountMatches = accountId.empty() || entry.accountId == accountId;
			return channelMatches && accountMatches;
			});

		if (routeIt != m_routes.end()) {
			return *routeIt;
		}

		if (!m_routes.empty()) {
			return m_routes.front();
		}

		return ChannelRouteEntry{
			.channel = channel.empty() ? "unknown" : channel,
			.accountId = accountId.empty() ? "default" : accountId,
			.agentId = "default",
			.sessionId = "main",
		};
	}

	ChannelLogoutResult GatewayChannelRegistry::Logout(const std::string& channel, const std::string& accountId) {
		ChannelLogoutResult result{};

		for (auto& account : m_accounts) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			if (!(channelMatches && accountMatches)) {
				continue;
			}

			account.connected = false;
			account.active = false;
			++result.affected;
		}

		if (result.affected > 0) {
			for (auto& status : m_status) {
				const bool channelMatches = channel.empty() || status.id == channel;
				if (!channelMatches) {
					continue;
				}

				status.connected = false;
			}
		}

		result.loggedOut = result.affected > 0;
		return result;
	}

	ChannelRouteEntry GatewayChannelRegistry::SetRoute(
		const std::string& channel,
		const std::string& accountId,
		const std::string& agentId,
		const std::string& sessionId) {
		const std::string resolvedChannel = channel.empty() ? "telegram" : channel;
		const std::string resolvedAccountId = accountId.empty() ? (resolvedChannel + ".default") : accountId;
		const std::string resolvedAgentId = agentId.empty() ? "default" : agentId;
		const std::string resolvedSessionId = sessionId.empty() ? "main" : sessionId;

		ChannelRouteEntry route{
			.channel = resolvedChannel,
			.accountId = resolvedAccountId,
			.agentId = resolvedAgentId,
			.sessionId = resolvedSessionId,
		};

		const auto it = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& entry) {
			return entry.channel == resolvedChannel && entry.accountId == resolvedAccountId;
			});

		if (it != m_routes.end()) {
			*it = route;
		}
		else {
			m_routes.push_back(route);
		}

		return route;
	}

} // namespace blazeclaw::gateway
