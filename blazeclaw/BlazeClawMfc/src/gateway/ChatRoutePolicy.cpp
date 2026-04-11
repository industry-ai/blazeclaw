#include "pch.h"
#include "ChatRoutePolicy.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
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
	}

	ChatRoutePolicy::Output ChatRoutePolicy::Resolve(const Input& input) const {
		const std::string mode = LowerTrim(input.clientMode);
		const std::string channel = LowerTrim(input.routeChannel);
		const std::string to = input.routeTo;

		if (!input.deliver) {
			return Output{};
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

		if (!IsLikelyChannelScopedSession(input.sessionKey)) {
			return Output{
				.originatingChannel = "internal",
				.originatingTo = {},
				.explicitDeliverRoute = false,
				.reasonCode = "session_not_channel_scoped",
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
