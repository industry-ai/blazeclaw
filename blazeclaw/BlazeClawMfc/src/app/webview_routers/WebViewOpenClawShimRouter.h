#pragma once

#include <string>

namespace blazeclaw::webview_routers {

	struct WebViewRouterContext;

	/// <summary>
	/// Handles OpenClaw WebSocket shim protocol messages from WebView.
	/// Routes: openclaw.ws.shim.ready, openclaw.ws.req (connect.challenge, connect, gateway methods).
	/// </summary>
	class WebViewOpenClawShimRouter {
	public:
		/// <summary>
		/// Route a WebView message to OpenClaw WS shim handlers.
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
		static bool HandleShimReady(
			const WebViewRouterContext& context,
			const std::string& messageJson);
		static bool HandleWsRequest(
			const WebViewRouterContext& context,
			const std::string& messageJson);
		static void HandleConnectChallenge(
			const WebViewRouterContext& context,
			const std::string& correlationId);
		static void HandleConnect(
			const WebViewRouterContext& context,
			const std::string& correlationId);
		static void PostErrorResponse(
			const WebViewRouterContext& context,
			const std::string& correlationId,
			const std::string& code,
			const std::string& message);
	};

} // namespace blazeclaw::webview_routers
