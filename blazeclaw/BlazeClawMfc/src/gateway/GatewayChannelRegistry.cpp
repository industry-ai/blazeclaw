#include "pch.h"
#include "GatewayChannelRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace blazeclaw::gateway {
	namespace {
		std::vector<ChannelAdapterDescriptor> BuildSeedAdapters() {
			return {
                ChannelAdapterDescriptor{
					.id = "whatsapp",
					.label = "WhatsApp",
					.defaultAccountId = "whatsapp.default",
				},
				ChannelAdapterDescriptor{.id = "telegram", .label = "Telegram", .defaultAccountId = "telegram.default"},
              ChannelAdapterDescriptor{.id = "slack", .label = "Slack", .defaultAccountId = "slack.default"},
				ChannelAdapterDescriptor{.id = "discord", .label = "Discord", .defaultAccountId = "discord.default"},
             ChannelAdapterDescriptor{
					.id = "msteams",
					.label = "Microsoft Teams",
					.defaultAccountId = "msteams.default",
				},
				ChannelAdapterDescriptor{
					.id = "googlechat",
					.label = "Google Chat",
					.defaultAccountId = "googlechat.default",
				},
				ChannelAdapterDescriptor{
					.id = "matrix",
					.label = "Matrix",
					.defaultAccountId = "matrix.default",
				},
				ChannelAdapterDescriptor{
					.id = "line",
					.label = "LINE",
					.defaultAccountId = "line.default",
				},
			};
		}
	}

	GatewayChannelRegistry::GatewayChannelRegistry() {
        m_adapters = BuildSeedAdapters();

		for (const auto& adapter : m_adapters) {
			m_status.push_back(ChannelStatusEntry{
				.id = adapter.id,
				.label = adapter.label,
				.connected = false,
				.accountCount = 1,
			});

			m_accounts.push_back(ChannelAccountEntry{
				.channel = adapter.id,
				.accountId = adapter.defaultAccountId,
				.label = adapter.label + " Default",
				.active = true,
				.connected = false,
			});

			m_routes.push_back(ChannelRouteEntry{
				.channel = adapter.id,
				.accountId = adapter.defaultAccountId,
				.agentId = "default",
				.sessionId = "main",
			});
		}

		LoadPersistedState();
		RecomputeStatus();
	}

	std::vector<ChannelAdapterDescriptor> GatewayChannelRegistry::ListAdapters() const {
		return m_adapters;
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

		RecomputeStatus();
		PersistState();

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

	ChannelRouteEntry GatewayChannelRegistry::GetRoute(const std::string& channel, const std::string& accountId) const {
		return ResolveRoute(channel, accountId);
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

		if (result.affected > 0) {
			RecomputeStatus();
			PersistState();
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

		RecomputeStatus();
		PersistState();

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

		RecomputeStatus();
		PersistState();

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

		RecomputeStatus();
		PersistState();

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

		RecomputeStatus();
		PersistState();

		return selected;
	}

	std::size_t GatewayChannelRegistry::ClearAccounts(const std::string& channel) {
		const std::size_t originalSize = m_accounts.size();
		m_accounts.erase(
			std::remove_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return channel.empty() || account.channel == channel;
			}),
			m_accounts.end());

		for (auto& status : m_status) {
			if (!channel.empty() && status.id != channel) {
				continue;
			}

			status.accountCount = std::count_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id;
			});
			status.connected = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id && account.connected;
			});
		}

		RecomputeStatus();
		PersistState();

		return originalSize - m_accounts.size();
	}

	std::size_t GatewayChannelRegistry::RestoreAccounts(const std::string& channel) {
       std::vector<ChannelAccountEntry> seededAccounts;
		seededAccounts.reserve(m_adapters.size());
		for (const auto& adapter : m_adapters) {
			seededAccounts.push_back(ChannelAccountEntry{
				.channel = adapter.id,
				.accountId = adapter.defaultAccountId,
				.label = adapter.label + " Default",
				.active = true,
				.connected = false,
			});
		}

		std::size_t restored = 0;
		for (const auto& seed : seededAccounts) {
			if (!channel.empty() && seed.channel != channel) {
				continue;
			}

			const bool exists = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == seed.channel && account.accountId == seed.accountId;
			});
			if (exists) {
				continue;
			}

			m_accounts.push_back(seed);
			++restored;
		}

		for (auto& status : m_status) {
			if (!channel.empty() && status.id != channel) {
				continue;
			}

			status.accountCount = std::count_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id;
			});
			status.connected = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id && account.connected;
			});
		}

		RecomputeStatus();
		PersistState();

		return restored;
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

		PersistState();

		return route;
	}

	ChannelRouteEntry GatewayChannelRegistry::PatchRoute(
		const std::string& channel,
		const std::string& accountId,
		const std::optional<std::string>& agentId,
		const std::optional<std::string>& sessionId,
		bool& updated) {
		updated = false;
		ChannelRouteEntry selected{};

		for (auto& route : m_routes) {
			const bool channelMatches = channel.empty() || route.channel == channel;
			const bool accountMatches = accountId.empty() || route.accountId == accountId;
			if (!(channelMatches && accountMatches)) {
				continue;
			}

			if (agentId.has_value() && !agentId->empty()) {
				route.agentId = agentId.value();
			}
			if (sessionId.has_value() && !sessionId->empty()) {
				route.sessionId = sessionId.value();
			}

			selected = route;
			updated = true;
			break;
		}

		if (!updated) {
			return ResolveRoute(channel, accountId);
		}

		PersistState();

		return selected;
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
        PersistState();
		return true;
	}

	ChannelRouteEntry GatewayChannelRegistry::RestoreRoute(const std::string& channel, const std::string& accountId, bool& restored) {
		restored = false;
       std::vector<ChannelRouteEntry> seededRoutes;
		seededRoutes.reserve(m_adapters.size());
		for (const auto& adapter : m_adapters) {
			seededRoutes.push_back(ChannelRouteEntry{
				.channel = adapter.id,
				.accountId = adapter.defaultAccountId,
				.agentId = "default",
				.sessionId = "main",
			});
		}

		const auto seededIt = std::find_if(seededRoutes.begin(), seededRoutes.end(), [&](const ChannelRouteEntry& seed) {
			const bool channelMatches = channel.empty() || seed.channel == channel;
			const bool accountMatches = accountId.empty() || seed.accountId == accountId;
			return channelMatches && accountMatches;
		});

		const ChannelRouteEntry fallback = ResolveRoute(channel, accountId);
		if (seededIt == seededRoutes.end()) {
			return fallback;
		}

		const auto existingIt = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& route) {
			return route.channel == seededIt->channel && route.accountId == seededIt->accountId;
		});

		if (existingIt != m_routes.end()) {
			return *existingIt;
		}

		m_routes.push_back(*seededIt);
		restored = true;
       PersistState();
		return *seededIt;
	}

	std::size_t GatewayChannelRegistry::ClearRoutes(const std::string& channel) {
		const std::size_t originalSize = m_routes.size();
		m_routes.erase(
			std::remove_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& route) {
				return channel.empty() || route.channel == channel;
				}),
			m_routes.end());
      PersistState();
		return originalSize - m_routes.size();
	}

	std::size_t GatewayChannelRegistry::RestoreRoutes(const std::string& channel) {
       std::vector<ChannelRouteEntry> seededRoutes;
		seededRoutes.reserve(m_adapters.size());
		for (const auto& adapter : m_adapters) {
			seededRoutes.push_back(ChannelRouteEntry{
				.channel = adapter.id,
				.accountId = adapter.defaultAccountId,
				.agentId = "default",
				.sessionId = "main",
			});
		}

		std::size_t restored = 0;
		for (const auto& seed : seededRoutes) {
			if (!channel.empty() && seed.channel != channel) {
				continue;
			}

			const bool exists = std::any_of(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& route) {
				return route.channel == seed.channel && route.accountId == seed.accountId;
			});

			if (exists) {
				continue;
			}

			m_routes.push_back(seed);
			++restored;
		}

		if (restored > 0) {
			PersistState();
		}

		return restored;
	}

	RouteResetResult GatewayChannelRegistry::ResetRoutes(const std::string& channel) {
		RouteResetResult result{};
		result.cleared = ClearRoutes(channel);
		result.restored = RestoreRoutes(channel);
		result.total = m_routes.size();
		return result;
	}

	bool GatewayChannelRegistry::RouteExists(const std::string& channel, const std::string& accountId) const {
		const auto it = std::find_if(m_routes.begin(), m_routes.end(), [&](const ChannelRouteEntry& entry) {
			const bool channelMatches = channel.empty() || entry.channel == channel;
			const bool accountMatches = accountId.empty() || entry.accountId == accountId;
			return channelMatches && accountMatches;
			});

		return it != m_routes.end();
	}

	std::filesystem::path GatewayChannelRegistry::PersistencePath() {
		return std::filesystem::path("blazeclaw") / "state" / "channels.state";
	}

	void GatewayChannelRegistry::LoadPersistedState() {
		const std::filesystem::path path = PersistencePath();
		std::ifstream input(path);
		if (!input.is_open()) {
			return;
		}

		std::vector<ChannelAccountEntry> loadedAccounts;
		std::vector<ChannelRouteEntry> loadedRoutes;

		std::string line;
		while (std::getline(input, line)) {
			if (line.empty()) {
				continue;
			}

			std::istringstream row(line);
			std::string tag;
			if (!std::getline(row, tag, '|')) {
				continue;
			}

			if (tag == "A") {
				std::string channel;
				std::string accountId;
				std::string label;
				std::string active;
				std::string connected;
				if (!std::getline(row, channel, '|') ||
					!std::getline(row, accountId, '|') ||
					!std::getline(row, label, '|') ||
					!std::getline(row, active, '|') ||
					!std::getline(row, connected)) {
					continue;
				}

				loadedAccounts.push_back(ChannelAccountEntry{
					.channel = channel,
					.accountId = accountId,
					.label = label,
					.active = active == "1" || active == "true",
					.connected = connected == "1" || connected == "true",
				});
				continue;
			}

			if (tag == "R") {
				std::string channel;
				std::string accountId;
				std::string agentId;
				std::string sessionId;
				if (!std::getline(row, channel, '|') ||
					!std::getline(row, accountId, '|') ||
					!std::getline(row, agentId, '|') ||
					!std::getline(row, sessionId)) {
					continue;
				}

				loadedRoutes.push_back(ChannelRouteEntry{
					.channel = channel,
					.accountId = accountId,
					.agentId = agentId,
					.sessionId = sessionId,
				});
			}
		}

		if (!loadedAccounts.empty()) {
			m_accounts = std::move(loadedAccounts);
		}

		if (!loadedRoutes.empty()) {
			m_routes = std::move(loadedRoutes);
		}
	}

	void GatewayChannelRegistry::PersistState() const {
		const std::filesystem::path path = PersistencePath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

		std::ofstream output(path, std::ios::out | std::ios::trunc);
		if (!output.is_open()) {
			return;
		}

		for (const auto& account : m_accounts) {
			output << "A|" << account.channel << "|" << account.accountId << "|" << account.label << "|"
				<< (account.active ? "1" : "0") << "|" << (account.connected ? "1" : "0") << "\n";
		}

		for (const auto& route : m_routes) {
			output << "R|" << route.channel << "|" << route.accountId << "|" << route.agentId << "|"
				<< route.sessionId << "\n";
		}
	}

	void GatewayChannelRegistry::RecomputeStatus() {
		for (const auto& adapter : m_adapters) {
			const auto statusIt = std::find_if(m_status.begin(), m_status.end(), [&](const ChannelStatusEntry& status) {
				return status.id == adapter.id;
			});

			if (statusIt == m_status.end()) {
				m_status.push_back(ChannelStatusEntry{
					.id = adapter.id,
					.label = adapter.label,
					.connected = false,
					.accountCount = 0,
				});
			}
		}

		for (auto& status : m_status) {
			status.accountCount = std::count_if(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id;
			});
			status.connected = std::any_of(m_accounts.begin(), m_accounts.end(), [&](const ChannelAccountEntry& account) {
				return account.channel == status.id && account.connected;
			});
		}
	}

} // namespace blazeclaw::gateway
