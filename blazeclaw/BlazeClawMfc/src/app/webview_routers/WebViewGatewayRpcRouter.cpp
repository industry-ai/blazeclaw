#include "pch.h"
#include "WebViewGatewayRpcRouter.h"
#include "WebViewRouterContext.h"
#include "WebViewToolLifecycleInstrumentation.h"
#include "../BlazeClawMFCApp.h"
#include "../EventTransport.h"
#include "../../gateway/GatewayJsonUtils.h"
#include "../../gateway/GatewayProtocolModels.h"

namespace blazeclaw::webview_routers {

	bool WebViewGatewayRpcRouter::RouteMessage(
		const WebViewRouterContext& context,
		const std::string& channel,
		const std::string& messageJson)
	{
		if (channel != "blazeclaw.gateway.rpc") {
			return false;
		}

		if (!context.eventTransport || !context.app) {
			return false;
		}

		std::string correlationId;
		if (!blazeclaw::gateway::json::FindStringField(messageJson, "id", correlationId)) {
			correlationId = "rpc-unknown";
		}

		std::string method;
		blazeclaw::gateway::json::FindStringField(messageJson, "method", method);

		std::string paramsJsonRaw;
		std::optional<std::string> paramsJson;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "params", paramsJsonRaw)) {
			paramsJson = blazeclaw::gateway::json::Trim(paramsJsonRaw);
		}

		// Delegate speech methods to WebViewSpeechRpcRouter
		if (IsSpeechMethod(method)) {
			return false; // Let the speech router handle this
		}

		// Tool lifecycle instrumentation: emit start event
		if (context.isToolExecuteMethod && context.isToolExecuteMethod(method)) {
			WebViewToolLifecycleInstrumentation::EmitToolStart(
				context,
				"blazeclaw.gateway.rpc",
				correlationId,
				paramsJson);
		}

		// Route request to gateway
		const blazeclaw::gateway::protocol::RequestFrame request{
			.id = correlationId,
			.method = method,
			.paramsJson = paramsJson,
		};

		const auto response = context.app->RouteGatewayRequest(request);

		// Handle terminal run IDs for skill path reporting
		if (method == "chat.events.poll" &&
			response.ok &&
			response.payloadJson.has_value())
		{
			std::string eventsRaw;
			if (blazeclaw::gateway::json::FindRawField(
				response.payloadJson.value(),
				"events",
				eventsRaw))
			{
				if (context.extractTerminalRunIds && context.reportRunSkillPathsToToolOutput) {
					for (const auto& runId : context.extractTerminalRunIds(eventsRaw)) {
						context.reportRunSkillPathsToToolOutput(runId);
					}
				}
			}
		}

		// Tool lifecycle instrumentation: emit result event
		if (context.isToolExecuteMethod && context.isToolExecuteMethod(method)) {
			WebViewToolLifecycleInstrumentation::EmitToolResult(
				context,
				"blazeclaw.gateway.rpc",
				correlationId,
				response);
		}

		// Post RPC result
		if (context.buildBridgeRpcResultJson) {
			const std::string responseJson = context.buildBridgeRpcResultJson(response, correlationId);
			context.eventTransport->EmitTopic(BridgeEventTopic::RpcResult, responseJson);
		}

		return true;
	}

	bool WebViewGatewayRpcRouter::IsSpeechMethod(const std::string& method)
	{
		return method == "speech.transcribe" ||
			method.rfind("gateway.speech.", 0) == 0;
	}

	void WebViewGatewayRpcRouter::PostRpcErrorResult(
		const WebViewRouterContext& context,
		const std::string& correlationId,
		const std::string& code,
		const std::string& message)
	{
		if (!context.eventTransport) {
			return;
		}

		const std::string errorJson =
			"{\"channel\":\"blazeclaw.gateway.rpc.result\",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":false,\"error\":{\"code\":" +
			JsonString(code) +
			",\"message\":" +
			JsonString(message) +
			"}}";

		context.eventTransport->EmitTopic(BridgeEventTopic::RpcResult, errorJson);
	}

	std::string WebViewGatewayRpcRouter::JsonString(const std::string& value)
	{
		std::string escaped = "\"";
		for (const char c : value) {
			switch (c) {
			case '"':  escaped += "\\\""; break;
			case '\\': escaped += "\\\\"; break;
			case '\b': escaped += "\\b"; break;
			case '\f': escaped += "\\f"; break;
			case '\n': escaped += "\\n"; break;
			case '\r': escaped += "\\r"; break;
			case '\t': escaped += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
					escaped += buf;
				}
				else {
					escaped += c;
				}
				break;
			}
		}
		escaped += "\"";
		return escaped;
	}

} // namespace blazeclaw::webview_routers
