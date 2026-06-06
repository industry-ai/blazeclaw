#include "pch.h"
#include "WebViewLifecyclePushRouter.h"
#include "WebViewRouterContext.h"
#include "../CBridge.h"
#include "../../gateway/GatewayJsonUtils.h"

namespace {
	std::string ToLowerAscii(const std::string& value) {
		std::string lowered;
		lowered.reserve(value.size());
		for (const char c : value) {
			lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		return lowered;
	}
}

namespace blazeclaw::webview_routers {

	bool WebViewLifecyclePushRouter::RouteMessage(
		const WebViewRouterContext& context,
		const std::string& channel,
		const std::string& messageJson)
	{
		if (channel == "blazeclaw.gateway.lifecycle.subscribe") {
			return HandleLifecycleSubscribe(context);
		}

		if (channel == "blazeclaw.gateway.chat.push.state") {
			return HandlePushState(context, messageJson);
		}

		if (channel == "blazeclaw.gateway.chat.push.event") {
			return HandlePushEvent(context, messageJson);
		}

		return false;
	}

	bool WebViewLifecyclePushRouter::HandleLifecycleSubscribe(
		const WebViewRouterContext& context)
	{
		if (!context.bridge) {
			return false;
		}

		context.bridge->ResetLifecycle();
		// Note: PumpBridgeLifecycle() is a view-level method that will be called by the main handler
		// after this router returns. The router only handles the bridge state reset.
		return true;
	}

	bool WebViewLifecyclePushRouter::HandlePushState(
		const WebViewRouterContext& context,
		const std::string& messageJson)
	{
		if (!context.bridge) {
			return false;
		}

		std::string state;
		std::string reason;
		blazeclaw::gateway::json::FindStringField(messageJson, "state", state);
		blazeclaw::gateway::json::FindStringField(messageJson, "reason", reason);

		const std::string lowered = ToLowerAscii(state);
		if (lowered == "connected" || lowered == "reconnected") {
			context.bridge->HandlePushConnected(
				reason.empty() ? "push-state-connected" : reason);
			return true;
		}

		if (lowered == "disconnected" || lowered == "degraded") {
			context.bridge->HandlePushDisconnected(
				reason.empty() ? "push-state-disconnected" : reason);
			return true;
		}

		return false;
	}

	bool WebViewLifecyclePushRouter::HandlePushEvent(
		const WebViewRouterContext& context,
		const std::string& messageJson)
	{
		if (!context.bridge) {
			return false;
		}

		std::string eventRaw;
		if (!blazeclaw::gateway::json::FindRawField(messageJson, "event", eventRaw)) {
			return false;
		}

		std::string seqRaw;
		std::optional<std::uint64_t> seq;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "seq", seqRaw)) {
			try {
				seq = static_cast<std::uint64_t>(
					std::stoull(blazeclaw::gateway::json::Trim(seqRaw)));
			}
			catch (...) {
				seq = std::nullopt;
			}
		}

		context.bridge->HandlePushChatEventFrame(eventRaw, seq);
		return true;
	}

} // namespace blazeclaw::webview_routers
