#include "pch.h"
#include "ChatControlPlaneService.h"

#include <algorithm>

namespace blazeclaw::gateway {

	ChatControlPlaneService::SendControlDecision
		ChatControlPlaneService::EvaluateSendControl(
			const SendControlInput& input) const {
		ChatRoutePolicy routePolicy;
		ToolEventRecipientPolicy toolEventPolicy;

		return SendControlDecision{
			.route = routePolicy.Resolve(
				ChatRoutePolicy::Input{
					.sessionKey = input.sessionKey,
					.deliver = input.deliver,
					.routeChannel = input.routeChannel,
					.routeTo = input.routeTo,
					.clientMode = input.clientMode,
				   .hasConnectedClient = input.hasConnectedClient,
					.mainKey = input.mainKey,
				}),
			.toolEvents = toolEventPolicy.Evaluate(
				ToolEventRecipientPolicy::Input{
					.clientCaps = input.clientCaps,
					.sessionKey = input.sessionKey,
					.runId = input.runId,
				 .hasRegisteredRecipient = input.hasRegisteredRecipient,
					.lateJoinRequested = input.lateJoinRequested,
				}),
		};
	}

	bool ChatControlPlaneService::ShouldPublishToolDelta(
		const std::string& delta,
		const SendControlDecision& decision) const {
		if (decision.toolEvents.wantsToolEvents) {
			return true;
		}

		const std::string marker = "tools.execute";
		if (delta.size() >= marker.size() &&
			delta.compare(0, marker.size(), marker) == 0) {
			return false;
		}

		return true;
	}

} // namespace blazeclaw::gateway
