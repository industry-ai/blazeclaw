#include "pch.h"
#include "WebViewOpenClawShimRouter.h"
#include "WebViewRouterContext.h"
#include "WebViewToolLifecycleInstrumentation.h"
#include "../CBridge.h"
#include "../BlazeClawMFCApp.h"
#include "../../gateway/GatewayJsonUtils.h"
#include "../../gateway/GatewayProtocolModels.h"

namespace blazeclaw::webview_routers {

	bool WebViewOpenClawShimRouter::RouteMessage(
		const WebViewRouterContext& context,
		const std::string& channel,
		const std::string& messageJson)
	{
		if (channel == "openclaw.ws.shim.ready") {
			return HandleShimReady(context, messageJson);
		}

		if (channel == "openclaw.ws.req") {
			return HandleWsRequest(context, messageJson);
		}

		return false;
	}

	bool WebViewOpenClawShimRouter::HandleShimReady(
		const WebViewRouterContext& context,
		const std::string& messageJson)
	{
		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				L"runtime.shim.ready",
				messageJson);
		}
		return true;
	}

	bool WebViewOpenClawShimRouter::HandleWsRequest(
		const WebViewRouterContext& context,
		const std::string& messageJson)
	{
		if (!context.bridge || !context.app) {
			return false;
		}

		context.bridge->IncrementReqCount();

		if (context.traceBridgeTraffic) {
			context.traceBridgeTraffic("ws.req.channel", messageJson);
		}
		if (context.appendChatProcedureStatusLine) {
			context.appendChatProcedureStatusLine(L"bridge.ws.req");
		}
		if (context.flushBridgeTraceIfNeeded) {
			context.flushBridgeTraceIfNeeded();
		}

		std::string frameRaw;
		if (!blazeclaw::gateway::json::FindRawField(messageJson, "frame", frameRaw)) {
			if (context.traceBridgeTraffic) {
				context.traceBridgeTraffic("ws.req.invalid", "missing frame field");
			}
			return true;
		}

		std::string frameType;
		blazeclaw::gateway::json::FindStringField(frameRaw, "type", frameType);
		if (frameType != "req") {
			if (context.traceBridgeTraffic) {
				context.traceBridgeTraffic("ws.req.ignored", frameType);
			}
			if (context.flushBridgeTraceIfNeeded) {
				context.flushBridgeTraceIfNeeded();
			}
			return true;
		}

		if (context.traceBridgeTraffic) {
			context.traceBridgeTraffic("ws.req", frameRaw);
		}

		std::string correlationId;
		if (!blazeclaw::gateway::json::FindStringField(frameRaw, "id", correlationId)) {
			correlationId = "openclaw-unknown";
		}

		std::string method;
		blazeclaw::gateway::json::FindStringField(frameRaw, "method", method);
		if (method.empty()) {
			if (context.traceBridgeTraffic) {
				context.traceBridgeTraffic("ws.req.invalid", "missing method");
			}
			if (context.appendChatProcedureStatusLine) {
				context.appendChatProcedureStatusLine(L"bridge.ws.req.invalid");
			}
			PostErrorResponse(context, correlationId, "invalid_frame", 
				"WebView bridge frame missing method.");
			return true;
		}

		if (method == "connect.challenge") {
			HandleConnectChallenge(context, correlationId);
			return true;
		}

		if (method == "connect") {
			HandleConnect(context, correlationId);
			return true;
		}

		std::optional<std::string> paramsJson;
		std::string paramsRaw;
		if (blazeclaw::gateway::json::FindRawField(frameRaw, "params", paramsRaw)) {
			paramsJson = blazeclaw::gateway::json::Trim(paramsRaw);
		}

		// Tool lifecycle instrumentation: emit start event
		if (context.isToolExecuteMethod && context.isToolExecuteMethod(method)) {
			WebViewToolLifecycleInstrumentation::EmitToolStart(
				context,
				"openclaw.ws.req",
				correlationId,
				paramsJson);
		}

		const blazeclaw::gateway::protocol::RequestFrame request{
			.id = correlationId,
			.method = method,
			.paramsJson = paramsJson,
		};
		const auto response = context.app->RouteGatewayRequest(request);

		if (context.traceBridgeTraffic) {
			context.traceBridgeTraffic("ws.req.route", method);
		}
		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(L"bridge.req.route", method);
		}

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
				"openclaw.ws.req",
				correlationId,
				response);
		}

		// Post response frame
		if (context.buildOpenClawWsResponseFrameJson && context.postOpenClawWsFrameJson) {
			const std::string responseFrameJson = 
				context.buildOpenClawWsResponseFrameJson(response, correlationId);
			context.postOpenClawWsFrameJson(responseFrameJson);
		}

		return true;
	}

	void WebViewOpenClawShimRouter::HandleConnectChallenge(
		const WebViewRouterContext& context,
		const std::string& correlationId)
	{
		if (!context.bridge) {
			return;
		}

		if (context.traceBridgeTraffic) {
			context.traceBridgeTraffic("ws.req.challenge", correlationId);
		}
		if (context.appendChatProcedureStatusLine) {
			context.appendChatProcedureStatusLine(L"bridge.connect.challenge");
		}
		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				L"runtime.handshake",
				"connect.challenge handled");
		}

		const std::uint64_t seq = context.bridge->NextEventSeq();
		const std::string eventFrame =
			"{\"type\":\"event\",\"event\":\"connect.challenge\","
			"\"payload\":{\"nonce\":\"blazeclaw-bridge\"},"
			"\"seq\":" +
			std::to_string(seq) +
			"}";

		if (context.postOpenClawWsFrameJson) {
			context.postOpenClawWsFrameJson(eventFrame);
		}
	}

	void WebViewOpenClawShimRouter::HandleConnect(
		const WebViewRouterContext& context,
		const std::string& correlationId)
	{
		if (context.traceBridgeTraffic) {
			context.traceBridgeTraffic("ws.req.connect", correlationId);
		}
		if (context.appendChatProcedureStatusLine) {
			context.appendChatProcedureStatusLine(L"bridge.connect");
		}
		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				L"runtime.handshake",
				"connect handled");
		}

		if (!context.buildOpenClawHelloPayloadJson || 
			!context.buildOpenClawWsResponseFrameJson ||
			!context.postOpenClawWsFrameJson) {
			return;
		}

		const blazeclaw::gateway::protocol::ResponseFrame helloResponse{
			.id = correlationId,
			.ok = true,
			.payloadJson = context.buildOpenClawHelloPayloadJson(),
			.error = std::nullopt,
		};

		const std::string responseFrameJson =
			context.buildOpenClawWsResponseFrameJson(helloResponse, correlationId);
		context.postOpenClawWsFrameJson(responseFrameJson);
	}

	void WebViewOpenClawShimRouter::PostErrorResponse(
		const WebViewRouterContext& context,
		const std::string& correlationId,
		const std::string& code,
		const std::string& message)
	{
		if (!context.buildOpenClawWsResponseFrameJson || !context.postOpenClawWsFrameJson) {
			return;
		}

		const blazeclaw::gateway::protocol::ResponseFrame errorResponse{
			.id = correlationId,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = blazeclaw::gateway::protocol::ErrorShape{
				.code = code,
				.message = message,
				.detailsJson = std::nullopt,
				.retryable = false,
				.retryAfterMs = std::nullopt,
			},
		};

		const std::string responseFrameJson =
			context.buildOpenClawWsResponseFrameJson(errorResponse, correlationId);
		context.postOpenClawWsFrameJson(responseFrameJson);
	}

} // namespace blazeclaw::webview_routers
