#include "pch.h"
#include "ChatRoutePolicy.h"

#include <algorithm>
#include <unordered_set>

namespace blazeclaw::gateway {
	namespace {
		constexpr std::size_t kSessionKeyMaxLength = 512;

		std::string LowerTrim(const std::string& value) {
			std::string out = value;
			out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](unsigned char ch) {
				return !std::isspace(ch);
				}));
			out.erase(std::find_if(out.rbegin(), out.rend(), [](unsigned char ch) {
				return !std::isspace(ch);
				}).base(), out.end());
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
				});
			return out;
		}

		bool IsLikelyChannelScopedSession(const std::string& sessionKey) {
			const std::string lower = LowerTrim(sessionKey);
			return lower.find("channel:") == 0 ||
				lower.find("direct:") == 0 ||
				lower.find("dm:") == 0 ||
				lower.find("group:") == 0;
		}

		bool IsSessionChannelHint(const std::string& scopeHead, const std::string& routeChannel) {
			if (scopeHead.empty() || routeChannel.empty()) {
				return false;
			}

			return scopeHead == routeChannel;
		}

		std::vector<std::string> SplitSessionScope(const std::string& sessionKey) {
			std::vector<std::string> parts;
			std::string current;
			for (char ch : sessionKey) {
				if (ch == ':') {
					if (!current.empty()) {
						parts.push_back(LowerTrim(current));
					}
					current.clear();
					if (parts.size() >= 3) {
						break;
					}
					continue;
				}

				current.push_back(ch);
			}
			if (!current.empty() && parts.size() < 3) {
				parts.push_back(LowerTrim(current));
			}

			return parts;
		}

		bool IsChannelAgnosticScope(const std::string& scopeHead) {
			static const std::unordered_set<std::string> kAgnosticScopes = {
				"main", "direct", "dm", "group", "channel", "cron", "run", "subagent", "acp", "thread", "topic"
			};
			return kAgnosticScopes.find(scopeHead) != kAgnosticScopes.end();
		}
	}

	ChatRoutePolicy::Output ChatRoutePolicy::Resolve(const Input& input) const {
		const std::string mode = LowerTrim(input.clientMode);
		const std::string channel = LowerTrim(input.routeChannel);
		const std::string to = LowerTrim(input.routeTo);
		const std::string sessionKey = LowerTrim(input.sessionKey);
		const std::string mainKey = LowerTrim(input.mainKey.empty() ? std::string("main") : input.mainKey);

		if (!input.deliver) {
			return Output{};
		}

		if (sessionKey.size() > kSessionKeyMaxLength) {
			return Output{
				.originatingChannel = "internal",
				.originatingTo = {},
				.explicitDeliverRoute = false,
				.reasonCode = "session_key_too_long",
			};
		}

		if (mode == "webchat" || mode == "control") {
			return Output{
				.originatingChannel = "internal",
				.originatingTo = {},
				.explicitDeliverRoute = false,
				.reasonCode = "webchat_no_inherit",
			};
		}

		if (channel.empty() || to.empty()) {
			return Output{
				.originatingChannel = "internal",
				.originatingTo = {},
				.explicitDeliverRoute = false,
				.reasonCode = "missing_route_context",
			};
		}

		const auto scopeParts = SplitSessionScope(sessionKey);
		const std::string scopeHead = scopeParts.empty() ? std::string{} : scopeParts.front();
		const bool isConfiguredMainScope = !scopeHead.empty() && scopeHead == mainKey;
		const bool channelScoped = IsLikelyChannelScopedSession(sessionKey);
		const bool isAgnosticScope = IsChannelAgnosticScope(scopeHead);
		const bool hasLegacyPeerShape =
			scopeParts.size() > 1 && IsSessionChannelHint(scopeParts[1], channel);

		const bool canInheritRoute =
			((channelScoped || hasLegacyPeerShape) && !isAgnosticScope) ||
			(isConfiguredMainScope && input.hasConnectedClient);

		if (!canInheritRoute) {
			return Output{
				.originatingChannel = "internal",
				.originatingTo = {},
				.explicitDeliverRoute = false,
			 .reasonCode = "session_not_route_eligible",
			};
		}

		return Output{
			.originatingChannel = channel,
			.originatingTo = to,
			.explicitDeliverRoute = true,
			.reasonCode = "explicit_route",
		};
	}

} // namespace blazeclaw::gateway
