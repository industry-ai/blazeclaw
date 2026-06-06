#pragma once

#include <string>

namespace blazeclaw::webview_routers {

	struct WebViewRouterContext;

	/// <summary>
	/// Handles WebView lifecycle and push-notification channel messages.
	/// Routes: blazeclaw.gateway.lifecycle.subscribe, blazeclaw.gateway.chat.push.state, blazeclaw.gateway.chat.push.event.
	/// </summary>
	class WebViewLifecyclePushRouter {
	public:
		/// <summary>
		/// Route a WebView message to lifecycle/push handlers.
		/// </summary>
		/// <param name="context">Router context with dependencies and callbacks.</param>
		/// <param name="channel">Channel identifier from the WebView message.</param>
		/// <param name="messageJson">Full JSON message from WebView.</param>
		/// <returns>True if the message was handled by this router, false otherwise.</returns>
		static bool RouteMessage(
			const WebViewRouterContext& context,
			const std::string& channel,
			const std::string& messageJson);

	private:
		static bool HandleLifecycleSubscribe(const WebViewRouterContext& context);
		static bool HandlePushState(
			const WebViewRouterContext& context,
			const std::string& messageJson);
		static bool HandlePushEvent(
			const WebViewRouterContext& context,
			const std::string& messageJson);
	};

} // namespace blazeclaw::webview_routers
