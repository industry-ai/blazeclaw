#pragma once

#include <string>

namespace blazeclaw {
namespace gateway {
namespace protocol {
	struct RequestFrame;
	struct ResponseFrame;
}
}
}

namespace blazeclaw::webview_routers {

	struct WebViewRouterContext;

	/// <summary>
	/// Handles gateway RPC channel messages (excluding speech methods).
	/// Routes: blazeclaw.gateway.rpc.
	/// Speech methods (speech.transcribe, gateway.speech.*) are delegated to WebViewSpeechRpcRouter.
	/// </summary>
	class WebViewGatewayRpcRouter {
	public:
		/// <summary>
		/// Route a WebView RPC message to gateway handlers.
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
		static bool IsSpeechMethod(const std::string& method);
		static void PostRpcErrorResult(
			const WebViewRouterContext& context,
			const std::string& correlationId,
			const std::string& code,
			const std::string& message);
		static std::string JsonString(const std::string& value);
	};

} // namespace blazeclaw::webview_routers
