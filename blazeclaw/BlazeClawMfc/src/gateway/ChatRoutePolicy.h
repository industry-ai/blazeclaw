#pragma once

#include <string>

namespace blazeclaw::gateway {

	class ChatRoutePolicy {
	public:
		struct Input {
			std::string sessionKey;
			bool deliver = false;
			std::string routeChannel;
			std::string routeTo;
			std::string clientMode;
			bool hasConnectedClient = false;
			std::string mainKey = "main";
		};

		struct Output {
			std::string originatingChannel = "internal";
			std::string originatingTo;
			bool explicitDeliverRoute = false;
			std::string reasonCode = "internal_default";
		};

		[[nodiscard]] Output Resolve(const Input& input) const;
	};

} // namespace blazeclaw::gateway
