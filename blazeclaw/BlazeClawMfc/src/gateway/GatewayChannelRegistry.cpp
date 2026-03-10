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

	ChannelAccountEntry GatewayChannelRegistry::GetAccount(const std::string& channel, const std::string& accountId) const {
		const auto it = std::find_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			return channelMatches && accountMatches;
			});

		if (it != m_accounts.end()) {
			return *it;
		}

		if (!m_accounts.empty()) {
			return m_accounts.front();
		}

		return ChannelAccountEntry{
			.channel = channel.empty() ? "unknown" : channel,
			.accountId = accountId.empty() ? "unknown.default" : accountId,
			.label = "Unknown Account",
			.active = false,
			.connected = false,
		};
	}

	ChannelAccountEntry GatewayChannelRegistry::CreateAccount(
		const std::string& channel,
		const std::string& accountId,
		const std::optional<std::string>& label,
		std::optional<bool> active,
		std::optional<bool> connected,
		bool& created) {
		const std::string resolvedChannel = channel.empty() ? "telegram" : channel;
		const std::string resolvedAccountId = accountId.empty() ? (resolvedChannel + ".new") : accountId;
		const std::string resolvedLabel = label.has_value() && !label.value().empty()
			? label.value()
			: (resolvedChannel + " Account");

		ChannelAccountEntry entry{
			.channel = resolvedChannel,
			.accountId = resolvedAccountId,
			.label = resolvedLabel,
			.active = active.value_or(true),
			.connected = connected.value_or(false),
		};

		const auto it = std::find_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
			return account.channel == resolvedChannel && account.accountId == resolvedAccountId;
			});

		created = it == m_accounts.end();
		if (created) {
			m_accounts.push_back(entry);
		}
		else {
			*it = entry;
		}

		const auto statusIt = std::find_if(m_status.begin(), m_status.end(), [&](const ChannelStatusEntry& status) {
			return status.id == resolvedChannel;
			});

		if (statusIt != m_status.end()) {
			statusIt->accountCount = std::count_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == resolvedChannel;
				});
			statusIt->connected = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == resolvedChannel && account.connected;
				});
		}

		return entry;
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

	ChannelAccountEntry GatewayChannelRegistry::ActivateAccount(const std::string& channel, const std::string& accountId, bool& activated) {
		activated = false;
		ChannelAccountEntry selected{};

		for (auto& account : m_accounts) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			if (!(channelMatches && accountMatches)) {
				continue;
			}

			account.active = true;
			account.connected = true;
			selected = account;
			activated = true;
			break;
		}

		if (!activated) {
			if (!m_accounts.empty()) {
				selected = m_accounts.front();
			}
			return selected;
		}

		for (auto& status : m_status) {
			if (status.id == selected.channel) {
				status.connected = true;
			}
		}

		return selected;
	}

	ChannelAccountEntry GatewayChannelRegistry::DeactivateAccount(const std::string& channel, const std::string& accountId, bool& deactivated) {
		deactivated = false;
		ChannelAccountEntry selected{};

		for (auto& account : m_accounts) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			if (!(channelMatches && accountMatches)) {
				continue;
			}

			account.active = false;
			account.connected = false;
			selected = account;
			deactivated = true;
			break;
		}

		if (!deactivated) {
			if (!m_accounts.empty()) {
				selected = m_accounts.front();
			}
			return selected;
		}

		for (auto& status : m_status) {
			if (status.id == selected.channel) {
				const bool hasConnectedAccount = std::any_of(
					m_accounts.begin(),
					m_accounts.end(),
					[&](const ChannelAccountEntry& account) {
						return account.channel == selected.channel && account.connected;
					});
				status.connected = hasConnectedAccount;
			}
		}

		return selected;
	}

	bool GatewayChannelRegistry::AccountExists(const std::string& channel, const std::string& accountId) const {
		const auto it = std::find_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			return channelMatches && accountMatches;
			});

		return it != m_accounts.end();
	}

	ChannelAccountEntry GatewayChannelRegistry::UpdateAccount(
		const std::string& channel,
		const std::string& accountId,
		const std::optional<std::string>& label,
		std::optional<bool> active,
		std::optional<bool> connected,
		bool& updated) {
		updated = false;
		ChannelAccountEntry selected{};

		for (auto& account : m_accounts) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			if (!(channelMatches && accountMatches)) {
				continue;
			}

			if (label.has_value()) {
				account.label = label.value();
			}
			if (active.has_value()) {
				account.active = active.value();
			}
			if (connected.has_value()) {
				account.connected = connected.value();
			}

			selected = account;
			updated = true;
			break;
		}

		if (!updated) {
			if (!m_accounts.empty()) {
				selected = m_accounts.front();
			}
			return selected;
		}

		for (auto& status : m_status) {
			if (status.id == selected.channel) {
				const bool hasConnectedAccount = std::any_of(
					m_accounts.begin(),
					m_accounts.end(),
					[&](const ChannelAccountEntry& account) {
						return account.channel == selected.channel && account.connected;
					});
				status.connected = hasConnectedAccount;
			}
		}

		return selected;
	}

	ChannelAccountEntry GatewayChannelRegistry::DeleteAccount(const std::string& channel, const std::string& accountId, bool& deleted) {
		deleted = false;
		ChannelAccountEntry selected{};

		const auto it = std::find_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
			const bool channelMatches = channel.empty() || account.channel == channel;
			const bool accountMatches = accountId.empty() || account.accountId == accountId;
			return channelMatches && accountMatches;
			});

		if (it == m_accounts.end()) {
			if (!m_accounts.empty()) {
				selected = m_accounts.front();
			}
			return selected;
		}

		selected = *it;
		const std::string removedChannel = selected.channel;
		m_accounts.erase(it);
		deleted = true;

		const auto statusIt = std::find_if(m_status.begin(), m_status.end(), [&](const ChannelStatusEntry& status) {
			return status.id == removedChannel;
			});
		if (statusIt != m_status.end()) {
			statusIt->accountCount = std::count_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == removedChannel;
				});
			statusIt->connected = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == removedChannel && account.connected;
				});
		}

		return selected;
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

	bool GatewayChannelRegistry::DeleteRoute(
		const std::string& channel,
		const std::string& accountId,
		ChannelRouteEntry& removedRoute) {
		const auto it = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& entry) {
			const bool channelMatches = channel.empty() || entry.channel == channel;
			const bool accountMatches = accountId.empty() || entry.accountId == accountId;
			return channelMatches && accountMatches;
			});

		if (it == m_routes.end()) {
			removedRoute = ResolveRoute(channel, accountId);
			return false;
		}

		removedRoute = *it;
		m_routes.erase(it);
		return true;
	}

	std::size_t GatewayChannelRegistry::ClearRoutes(const std::string& channel) {
		const std::size_t originalSize = m_routes.size();
		m_routes.erase(
			std::remove_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& route) {
				return channel.empty() || route.channel == channel;
				}),
			m_routes.end());
		return originalSize - m_routes.size();
	}

	bool GatewayChannelRegistry::RouteExists(const std::string& channel, const std::string& accountId) const {
		const auto it = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& entry) {
			const bool channelMatches = channel.empty() || entry.channel == channel;
			const bool accountMatches = accountId.empty() || entry.accountId == accountId;
			return channelMatches && accountMatches;
			});

		return it != m_routes.end();
	}

} // namespace blazeclaw::gateway
