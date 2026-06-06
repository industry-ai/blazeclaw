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
	/// Handles speech RPC methods from blazeclaw.gateway.rpc channel.
	/// Routes: speech.transcribe, gateway.speech.startRecording, gateway.speech.stopRecording, etc.
	/// Manages async speech transcription with lifecycle events and window message completion.
	/// </summary>
	class WebViewSpeechRpcRouter {
	public:
		/// <summary>
		/// Route a speech RPC message to speech handlers.
		/// </summary>
		/// <param name="context">Router context with dependencies and callbacks.</param>
		/// <param name="method">RPC method name.</param>
		/// <param name="correlationId">Request correlation ID.</param>
		/// <param name="paramsJson">Optional request parameters JSON.</param>
		/// <param name="request">Full gateway request frame.</param>
		/// <returns>True if the message was handled by this router, false otherwise.</returns>
		static bool RouteMessage(
			const WebViewRouterContext& context,
			const std::string& method,
			const std::string& correlationId,
			const std::optional<std::string>& paramsJson,
			const blazeclaw::gateway::protocol::RequestFrame& request);

	private:
		static bool HandleSpeechTranscribe(
			const WebViewRouterContext& context,
			const std::string& correlationId,
			const std::optional<std::string>& paramsJson,
			const blazeclaw::gateway::protocol::RequestFrame& request);

		static void HandleSpeechRecordingMethod(
			const WebViewRouterContext& context,
			const std::string& method,
			const std::string& correlationId,
			const std::optional<std::string>& paramsJson,
			const blazeclaw::gateway::protocol::ResponseFrame& response);

		static void PostRpcErrorResult(
			const WebViewRouterContext& context,
			const std::string& correlationId,
			const std::string& code,
			const std::string& message);

		static std::string JsonString(const std::string& value);
	};

} // namespace blazeclaw::webview_routers
