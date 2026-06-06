#pragma once

#include <functional>
#include <string>

// Forward declarations
class CBridge;
class CEventTransport;
class CBlazeClawMFCApp;

namespace blazeclaw {
namespace gateway {
namespace protocol {
	struct ResponseFrame;
}
}
}

namespace blazeclaw::webview_routers {

	/// <summary>
	/// Shared context and dependencies for WebView message channel routers.
	/// Provides access to bridge state, event transport, diagnostics callbacks,
	/// and gateway app reference needed to route and handle WebView messages.
	/// </summary>
	struct WebViewRouterContext {
		// Core dependencies
		CBridge* bridge = nullptr;
		CEventTransport* eventTransport = nullptr;
		CBlazeClawMFCApp* app = nullptr;
		HWND viewHwnd = nullptr;

		// Session state
		std::string bridgeSessionId;

		// Diagnostics callbacks
		std::function<void(const wchar_t* stage)> appendChatProcedureStatusLine;
		std::function<void(const wchar_t* stage, const std::string& detail)> appendChatProcedureStatusLineWithDetail;
		std::function<void(const wchar_t* stage, const std::string& detail)> appendFindSkillPathStatus;
		std::function<void(const std::string& event, const std::string& detail)> traceBridgeTraffic;
		std::function<void(const std::string& event, const std::string& detail)> traceSpeechBridgeOrder;
		std::function<void()> flushBridgeTraceIfNeeded;

		// WebView communication callbacks
		std::function<void(const std::wstring& jsonMessage)> postBridgeMessageJson;
		std::function<void(const std::string& frameJson)> postOpenClawWsFrameJson;

		// Gateway response builders
		std::function<std::string(
			const blazeclaw::gateway::protocol::ResponseFrame& response,
			const std::string& correlationId)> buildOpenClawWsResponseFrameJson;
		std::function<std::string()> buildOpenClawHelloPayloadJson;
		std::function<std::string(
			const blazeclaw::gateway::protocol::ResponseFrame& response,
			const std::string& correlationId)> buildBridgeRpcResultJson;

		// Tool lifecycle helpers
		std::function<std::string(const std::optional<std::string>& paramsJson)> buildToolStartDetail;
		std::function<std::string(const blazeclaw::gateway::protocol::ResponseFrame& response)> buildToolResultDetail;

		// Speech lifecycle helpers
		std::function<std::string(
			const std::string& stage,
			const std::string& sessionId,
			const std::string& runId,
			const std::string& audioPath,
			const std::string& recognizedText,
			const std::string& errorMessage,
			std::uint64_t latencyMs,
			bool isFinal,
			const std::string& provider,
			const std::string& modelId,
			const std::string& category)> buildSpeechLifecyclePayloadJson;
		std::function<void(const std::string& payloadJson)> emitSpeechLifecycleEvent;

		// Utility helpers
		std::function<std::vector<std::string>(const std::string& eventsJson)> extractTerminalRunIds;
		std::function<void(const std::string& runId)> reportRunSkillPathsToToolOutput;
		std::function<bool(const std::string& method)> isToolExecuteMethod;
	};

} // namespace blazeclaw::webview_routers
