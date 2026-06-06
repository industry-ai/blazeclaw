#pragma once

#include <optional>
#include <string>

namespace blazeclaw {
namespace gateway {
namespace protocol {
	struct ResponseFrame;
}
}
}

namespace blazeclaw::webview_routers {

	struct WebViewRouterContext;

	/// <summary>
	/// Shared instrumentation for tool lifecycle events across WebView channel routers.
	/// Emits tool execution start/result events to the ToolsLifecycle topic,
	/// appends diagnostic status lines, and synchronizes tool path reporting.
	/// </summary>
	class WebViewToolLifecycleInstrumentation {
	public:
		/// <summary>
		/// Emit tool execution start event.
		/// Called when a tool execution method is detected in a WebView message.
		/// </summary>
		/// <param name="context">Router context with event transport and diagnostics callbacks.</param>
		/// <param name="sourceChannel">Source channel identifier (e.g., "openclaw.ws.req", "blazeclaw.gateway.rpc").</param>
		/// <param name="correlationId">Request correlation ID.</param>
		/// <param name="paramsJson">Tool execution parameters JSON.</param>
		static void EmitToolStart(
			const WebViewRouterContext& context,
			const std::string& sourceChannel,
			const std::string& correlationId,
			const std::optional<std::string>& paramsJson);

		/// <summary>
		/// Emit tool execution result event.
		/// Called when a tool execution completes (success or error).
		/// </summary>
		/// <param name="context">Router context with event transport and diagnostics callbacks.</param>
		/// <param name="sourceChannel">Source channel identifier.</param>
		/// <param name="correlationId">Request correlation ID.</param>
		/// <param name="response">Gateway response frame with tool execution result.</param>
		static void EmitToolResult(
			const WebViewRouterContext& context,
			const std::string& sourceChannel,
			const std::string& correlationId,
			const blazeclaw::gateway::protocol::ResponseFrame& response);

		/// <summary>
		/// Build tool lifecycle start event JSON payload.
		/// </summary>
		static std::string BuildToolLifecycleStartJson(
			const std::string& sourceChannel,
			const std::string& correlationId,
			const std::optional<std::string>& paramsJson);

		/// <summary>
		/// Build tool lifecycle result event JSON payload.
		/// </summary>
		static std::string BuildToolLifecycleResultJson(
			const std::string& sourceChannel,
			const std::string& correlationId,
			const blazeclaw::gateway::protocol::ResponseFrame& response);
	};

} // namespace blazeclaw::webview_routers
