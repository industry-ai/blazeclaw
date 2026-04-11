#pragma once

#include "ChatRoutePolicy.h"
#include "ToolEventRecipientPolicy.h"

#include <string>
#include <vector>

namespace blazeclaw::gateway {

	class ChatControlPlaneService {
	public:
		struct SendControlInput {
			std::string sessionKey;
			bool deliver = false;
			std::string routeChannel;
			std::string routeTo;
			std::string clientMode;
			bool hasConnectedClient = false;
			std::string mainKey = "main";
			std::vector<std::string> clientCaps;
			std::string runId;
			bool hasRegisteredRecipient = false;
			bool lateJoinRequested = false;
		};

		struct SendControlDecision {
			ChatRoutePolicy::Output route;
			ToolEventRecipientPolicy::Output toolEvents;
		};

		[[nodiscard]] SendControlDecision EvaluateSendControl(
			const SendControlInput& input) const;

		[[nodiscard]] bool ShouldPublishToolDelta(
			const std::string& delta,
			const SendControlDecision& decision) const;
	};

} // namespace blazeclaw::gateway
