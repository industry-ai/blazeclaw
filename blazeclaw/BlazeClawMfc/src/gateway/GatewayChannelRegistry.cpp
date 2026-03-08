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

} // namespace blazeclaw::gateway
