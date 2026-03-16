#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {
    namespace {
        std::string EscapeJsonLocal(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size() + 8);

            for (const char ch : value) {
                switch (ch) {
                case '"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(ch);
                    break;
                }
            }

            return escaped;
        }

        std::string ExtractStringParamLocal(
            const std::optional<std::string>& paramsJson,
            const std::string& fieldName) {
            if (!paramsJson.has_value()) {
                return {};
            }

            const std::string token = "\"" + fieldName + "\"";
            const std::string& params = paramsJson.value();
            const std::size_t keyPos = params.find(token);
            if (keyPos == std::string::npos) {
                return {};
            }

            std::size_t valuePos = params.find(':', keyPos + token.size());
            if (valuePos == std::string::npos) {
                return {};
            }

            valuePos = params.find('"', valuePos);
            if (valuePos == std::string::npos) {
                return {};
            }

            const std::size_t endQuote = params.find('"', valuePos + 1);
            if (endQuote == std::string::npos) {
                return {};
            }

            return params.substr(valuePos + 1, endQuote - valuePos - 1);
        }

        std::optional<bool> ExtractBooleanParamLocal(
            const std::optional<std::string>& paramsJson,
            const std::string& fieldName) {
            if (!paramsJson.has_value()) {
                return std::nullopt;
            }

            const std::string token = "\"" + fieldName + "\"";
            const std::string& params = paramsJson.value();
            const std::size_t keyPos = params.find(token);
            if (keyPos == std::string::npos) {
                return std::nullopt;
            }

            std::size_t valuePos = params.find(':', keyPos + token.size());
            if (valuePos == std::string::npos) {
                return std::nullopt;
            }

            ++valuePos;
            while (valuePos < params.size() &&
                std::isspace(static_cast<unsigned char>(params[valuePos])) != 0) {
                ++valuePos;
            }

            if (params.compare(valuePos, 4, "true") == 0) {
                return true;
            }

            if (params.compare(valuePos, 5, "false") == 0) {
                return false;
            }

            return std::nullopt;
        }

        std::string SerializeChannelAdapterLocal(const ChannelAdapterDescriptor& adapter) {
            return "{\"id\":\"" + EscapeJsonLocal(adapter.id) +
                "\",\"label\":\"" + EscapeJsonLocal(adapter.label) +
                "\",\"defaultAccountId\":\"" +
                EscapeJsonLocal(adapter.defaultAccountId) + "\"}";
        }

        std::string SerializeChannelStatusLocal(const ChannelStatusEntry& channel) {
            return "{\"id\":\"" + EscapeJsonLocal(channel.id) +
                "\",\"label\":\"" + EscapeJsonLocal(channel.label) +
                "\",\"connected\":" +
                std::string(channel.connected ? "true" : "false") +
                ",\"accounts\":" + std::to_string(channel.accountCount) + "}";
        }

        std::string SerializeChannelAccountLocal(const ChannelAccountEntry& account) {
            return "{\"channel\":\"" + EscapeJsonLocal(account.channel) +
                "\",\"accountId\":\"" + EscapeJsonLocal(account.accountId) +
                "\",\"label\":\"" + EscapeJsonLocal(account.label) +
                "\",\"active\":" +
                std::string(account.active ? "true" : "false") +
                ",\"connected\":" +
                std::string(account.connected ? "true" : "false") + "}";
        }

        std::string SerializeChannelRouteLocal(const ChannelRouteEntry& route) {
            return "{\"channel\":\"" + EscapeJsonLocal(route.channel) +
                "\",\"accountId\":\"" + EscapeJsonLocal(route.accountId) +
                "\",\"agentId\":\"" + EscapeJsonLocal(route.agentId) +
                "\",\"sessionId\":\"" + EscapeJsonLocal(route.sessionId) + "\"}";
        }
    }

    void GatewayHost::RegisterChannelsHandlers() {
        m_dispatcher.Register("gateway.channels.adapters.list", [this](const protocol::RequestFrame& request) {
            const auto adapters = m_channelRegistry.ListAdapters();
            std::string adaptersJson = "[";
            for (std::size_t i = 0; i < adapters.size(); ++i) {
                if (i > 0) {
                    adaptersJson += ",";
                }
               adaptersJson += SerializeChannelAdapterLocal(adapters[i]);
            }
            adaptersJson += "]";

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"adapters\":" + adaptersJson +
                    ",\"count\":" + std::to_string(adapters.size()) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.reset", [this](const protocol::RequestFrame& request) {
          const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::size_t cleared = m_channelRegistry.ClearAccounts(channel);
            const std::size_t restored = m_channelRegistry.RestoreAccounts(channel);
            const std::size_t total = m_channelRegistry.ListAccounts().size();

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cleared\":" + std::to_string(cleared) +
                    ",\"restored\":" + std::to_string(restored) +
                    ",\"total\":" + std::to_string(total) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.restore", [this](const protocol::RequestFrame& request) {
          const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::size_t restored = m_channelRegistry.RestoreAccounts(channel);
            const std::size_t total = m_channelRegistry.ListAccounts().size();

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"restored\":" + std::to_string(restored) +
                    ",\"total\":" + std::to_string(total) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.reset", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            ChannelRouteEntry removedRoute;
            const bool deleted =
                m_channelRegistry.DeleteRoute(channel, accountId, removedRoute);
            bool restored = false;
            const ChannelRouteEntry route =
                m_channelRegistry.RestoreRoute(channel, accountId, restored);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":" + SerializeChannelRouteLocal(route) +
                    ",\"deleted\":" + std::string(deleted ? "true" : "false") +
                    ",\"restored\":" + std::string(restored ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.count", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto accounts = m_channelRegistry.ListAccounts();
            const std::size_t count = static_cast<std::size_t>(std::count_if(
                accounts.begin(),
                accounts.end(),
                [&](const ChannelAccountEntry& account) {
                    return channel.empty() || account.channel == channel;
                }));

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"count\":" + std::to_string(count) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.clear", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::size_t cleared = m_channelRegistry.ClearAccounts(channel);
            const std::size_t remaining = m_channelRegistry.ListAccounts().size();

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cleared\":" + std::to_string(cleared) +
                    ",\"remaining\":" + std::to_string(remaining) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.routes.reset", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const RouteResetResult result = m_channelRegistry.ResetRoutes(channel);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cleared\":" + std::to_string(result.cleared) +
                    ",\"restored\":" + std::to_string(result.restored) +
                    ",\"total\":" + std::to_string(result.total) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.routes.count", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto routes = m_channelRegistry.ListRoutes();
            const std::size_t count = static_cast<std::size_t>(std::count_if(
                routes.begin(),
                routes.end(),
                [&](const ChannelRouteEntry& route) {
                    return channel.empty() || route.channel == channel;
                }));

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"count\":" + std::to_string(count) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.restore", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            bool restored = false;
            const ChannelRouteEntry route =
                m_channelRegistry.RestoreRoute(channel, accountId, restored);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":" + SerializeChannelRouteLocal(route) +
                    ",\"restored\":" + std::string(restored ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.patch", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const bool hasAgentId = request.paramsJson.has_value() &&
                request.paramsJson.value().find("\"agentId\"") != std::string::npos;
            const bool hasSessionId = request.paramsJson.has_value() &&
                request.paramsJson.value().find("\"sessionId\"") != std::string::npos;
            const std::optional<std::string> agentId = hasAgentId
                ? std::optional<std::string>(ExtractStringParamLocal(request.paramsJson, "agentId"))
                : std::nullopt;
            const std::optional<std::string> sessionId = hasSessionId
                ? std::optional<std::string>(ExtractStringParamLocal(request.paramsJson, "sessionId"))
                : std::nullopt;
            bool updated = false;
            const ChannelRouteEntry route = m_channelRegistry.PatchRoute(
                channel,
                accountId,
                agentId,
                sessionId,
                updated);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":" + SerializeChannelRouteLocal(route) +
                    ",\"updated\":" + std::string(updated ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.get", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const ChannelRouteEntry route = m_channelRegistry.GetRoute(channel, accountId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"route\":" + SerializeChannelRouteLocal(route) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.routes.restore", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::size_t restored = m_channelRegistry.RestoreRoutes(channel);
            const std::size_t total = m_channelRegistry.ListRoutes().size();

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"restored\":" + std::to_string(restored) +
                    ",\"total\":" + std::to_string(total) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.routes.clear", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::size_t cleared = m_channelRegistry.ClearRoutes(channel);
            const std::size_t remaining = m_channelRegistry.ListRoutes().size();

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cleared\":" + std::to_string(cleared) +
                    ",\"remaining\":" + std::to_string(remaining) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.delete", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            bool deleted = false;
            const ChannelAccountEntry account =
                m_channelRegistry.DeleteAccount(channel, accountId, deleted);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"account\":" + SerializeChannelAccountLocal(account) +
                    ",\"deleted\":" + std::string(deleted ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.create", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const bool hasLabel = request.paramsJson.has_value() &&
                request.paramsJson.value().find("\"label\"") != std::string::npos;
            const std::optional<std::string> label = hasLabel
                ? std::optional<std::string>(ExtractStringParamLocal(request.paramsJson, "label"))
                : std::nullopt;
            const std::optional<bool> active = ExtractBooleanParamLocal(request.paramsJson, "active");
            const std::optional<bool> connected =
                ExtractBooleanParamLocal(request.paramsJson, "connected");
            bool created = false;
            const ChannelAccountEntry account = m_channelRegistry.CreateAccount(
                channel,
                accountId,
                label,
                active,
                connected,
                created);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"account\":" + SerializeChannelAccountLocal(account) +
                    ",\"created\":" + std::string(created ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.get", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const ChannelAccountEntry account = m_channelRegistry.GetAccount(channel, accountId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"account\":" + SerializeChannelAccountLocal(account) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.update", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const bool hasLabel = request.paramsJson.has_value() &&
                request.paramsJson.value().find("\"label\"") != std::string::npos;
            const std::optional<std::string> label = hasLabel
                ? std::optional<std::string>(ExtractStringParamLocal(request.paramsJson, "label"))
                : std::nullopt;
            const std::optional<bool> active = ExtractBooleanParamLocal(request.paramsJson, "active");
            const std::optional<bool> connected =
                ExtractBooleanParamLocal(request.paramsJson, "connected");
            bool updated = false;
            const ChannelAccountEntry account = m_channelRegistry.UpdateAccount(
                channel,
                accountId,
                label,
                active,
                connected,
                updated);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"account\":" + SerializeChannelAccountLocal(account) +
                    ",\"updated\":" + std::string(updated ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.exists", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const bool exists = m_channelRegistry.AccountExists(channel, accountId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"accountId\":\"" +
                    EscapeJsonLocal(accountId.empty() ? "*" : accountId) +
                    "\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.deactivate", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            bool deactivated = false;
            const ChannelAccountEntry account =
                m_channelRegistry.DeactivateAccount(channel, accountId, deactivated);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"account\":" + SerializeChannelAccountLocal(account) +
                    ",\"deactivated\":" + std::string(deactivated ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts.activate", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            bool activated = false;
            const ChannelAccountEntry account =
                m_channelRegistry.ActivateAccount(channel, accountId, activated);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"account\":" + SerializeChannelAccountLocal(account) +
                    ",\"activated\":" + std::string(activated ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.exists", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const bool exists = m_channelRegistry.RouteExists(channel, accountId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"accountId\":\"" +
                    EscapeJsonLocal(accountId.empty() ? "*" : accountId) +
                    "\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.delete", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            ChannelRouteEntry removedRoute;
            const bool deleted = m_channelRegistry.DeleteRoute(channel, accountId, removedRoute);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":" + SerializeChannelRouteLocal(removedRoute) +
                    ",\"deleted\":" + std::string(deleted ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.set", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const std::string agentId = ExtractStringParamLocal(request.paramsJson, "agentId");
            const std::string sessionId = ExtractStringParamLocal(request.paramsJson, "sessionId");
            const ChannelRouteEntry route =
                m_channelRegistry.SetRoute(channel, accountId, agentId, sessionId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":" + SerializeChannelRouteLocal(route) +
                    ",\"saved\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.logout", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string accountId = ExtractStringParamLocal(request.paramsJson, "accountId");
            const ChannelLogoutResult result = m_channelRegistry.Logout(channel, accountId);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"loggedOut\":" +
                    std::string(result.loggedOut ? "true" : "false") +
                    ",\"affected\":" + std::to_string(result.affected) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.accounts", [this](const protocol::RequestFrame& request) {
            const std::string channelFilter = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto accounts = m_channelRegistry.ListAccounts();
            std::string accountsJson = "[";
            bool first = true;
            for (std::size_t i = 0; i < accounts.size(); ++i) {
                if (!channelFilter.empty() && accounts[i].channel != channelFilter) {
                    continue;
                }

                if (!first) {
                    accountsJson += ",";
                }

                accountsJson += SerializeChannelAccountLocal(accounts[i]);
                first = false;
            }

            accountsJson += "]";

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"accounts\":" + accountsJson + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.status.get", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto statuses = m_channelRegistry.ListStatus();
            ChannelStatusEntry selected{};
            bool found = false;
            for (const auto& status : statuses) {
                if (!channel.empty() && status.id != channel) {
                    continue;
                }
                selected = status;
                found = true;
                break;
            }
            if (!found) {
                if (!statuses.empty()) {
                    selected = statuses.front();
                }
                else {
                    selected = ChannelStatusEntry{
                        .id = channel.empty() ? "unknown" : channel,
                        .label = "Unknown",
                        .connected = false,
                        .accountCount = 0,
                    };
                }
            }

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"channel\":" + SerializeChannelStatusLocal(selected) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.status.exists", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto statuses = m_channelRegistry.ListStatus();
            const bool exists = std::any_of(
                statuses.begin(),
                statuses.end(),
                [&](const ChannelStatusEntry& status) {
                    return channel.empty() || status.id == channel;
                });

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.status.count", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto statuses = m_channelRegistry.ListStatus();
            const std::size_t count = static_cast<std::size_t>(std::count_if(
                statuses.begin(),
                statuses.end(),
                [&](const ChannelStatusEntry& status) {
                    return channel.empty() || status.id == channel;
                }));

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channel\":\"" + EscapeJsonLocal(channel.empty() ? "*" : channel) +
                    "\",\"count\":" + std::to_string(count) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.route.resolve", [this](const protocol::RequestFrame& request) {
            const std::string channel = ExtractStringParamLocal(request.paramsJson, "channel");
            const std::string account = ExtractStringParamLocal(request.paramsJson, "accountId");
            const std::string sessionId = ExtractStringParamLocal(request.paramsJson, "sessionId");
            const std::string agentId = ExtractStringParamLocal(request.paramsJson, "agentId");
            const ChannelRouteEntry route =
                m_channelRegistry.ResolveRoute(channel, account);
            const SessionEntry session =
                m_sessionRegistry.Resolve(sessionId.empty() ? route.sessionId : sessionId);
            const AgentEntry agent =
                m_agentRegistry.Get(agentId.empty() ? route.agentId : agentId);
            const ChannelRouteEntry resolvedRoute{
                .channel = route.channel,
                .accountId = route.accountId,
                agentId = agent.id,
                sessionId = session.id,
            };

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"route\":" + SerializeChannelRouteLocal(resolvedRoute) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.routes", [this](const protocol::RequestFrame& request) {
            const std::string channelFilter = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto routes = m_channelRegistry.ListRoutes();
            std::string routesJson = "[";
            bool first = true;
            for (std::size_t i = 0; i < routes.size(); ++i) {
                if (!channelFilter.empty() && routes[i].channel != channelFilter) {
                    continue;
                }

                if (!first) {
                    routesJson += ",";
                }

                routesJson += SerializeChannelRouteLocal(routes[i]);
                first = false;
            }

            routesJson += "]";

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"routes\":" + routesJson + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.channels.status", [this](const protocol::RequestFrame& request) {
            const std::string channelFilter = ExtractStringParamLocal(request.paramsJson, "channel");
            const auto channels = m_channelRegistry.ListStatus();
            std::string channelsJson = "[";
            bool first = true;
            for (std::size_t i = 0; i < channels.size(); ++i) {
                if (!channelFilter.empty() && channels[i].id != channelFilter) {
                    continue;
                }

                if (!first) {
                    channelsJson += ",";
                }

                channelsJson += SerializeChannelStatusLocal(channels[i]);
                first = false;
            }

            channelsJson += "]";

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"channels\":" + channelsJson + "}",
                .error = std::nullopt,
            };
            });
    }

} // namespace blazeclaw::gateway

