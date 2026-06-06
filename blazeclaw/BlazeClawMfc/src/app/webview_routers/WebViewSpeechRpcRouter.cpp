#include "pch.h"
#include "WebViewSpeechRpcRouter.h"
#include "WebViewRouterContext.h"
#include "../BlazeClawMFCApp.h"
#include "../EventTransport.h"
#include "../../gateway/GatewayJsonUtils.h"
#include "../../gateway/GatewayProtocolModels.h"

namespace blazeclaw::webview_routers {

	bool WebViewSpeechRpcRouter::RouteMessage(
		const WebViewRouterContext& context,
		const std::string& method,
		const std::string& correlationId,
		const std::optional<std::string>& paramsJson,
		const blazeclaw::gateway::protocol::RequestFrame& request)
	{
		if (!context.eventTransport || !context.app) {
			return false;
		}

		// Handle speech.transcribe method
		if (method == "speech.transcribe") {
			return HandleSpeechTranscribe(context, correlationId, paramsJson, request);
		}

		// Handle gateway.speech.* methods
		if (method.rfind("gateway.speech.", 0) == 0) {
			const auto response = context.app->RouteGatewayRequest(request);
			HandleSpeechRecordingMethod(context, method, correlationId, paramsJson, response);

			// Post RPC result
			if (context.buildBridgeRpcResultJson) {
				const std::string responseJson = context.buildBridgeRpcResultJson(response, correlationId);
				context.eventTransport->EmitTopic(BridgeEventTopic::RpcResult, responseJson);
			}
			return true;
		}

		return false;
	}

	bool WebViewSpeechRpcRouter::HandleSpeechTranscribe(
		const WebViewRouterContext& context,
		const std::string& correlationId,
		const std::optional<std::string>& paramsJson,
		const blazeclaw::gateway::protocol::RequestFrame& request)
	{
		// Validate view HWND
		if (context.viewHwnd == nullptr) {
			PostRpcErrorResult(context, correlationId, "view_unavailable",
				"Speech transcription view unavailable.");
			return true;
		}

		// Extract session ID
		std::string sessionId = context.bridgeSessionId;
		if (paramsJson.has_value()) {
			std::string extractedSessionId;
			if (blazeclaw::gateway::json::FindStringField(paramsJson.value(), "sessionId", extractedSessionId) &&
				!extractedSessionId.empty()) {
				sessionId = extractedSessionId;
			}
		}

		// Extract run ID
		std::string runId;
		if (paramsJson.has_value()) {
			blazeclaw::gateway::json::FindStringField(paramsJson.value(), "runId", runId);
		}

		// Parse streaming artifact parameters
		bool streamingRequest = false;
		std::string artifactHandoffMode;
		std::string artifactStreamId;
		std::uint64_t artifactSequenceStart = 0;
		std::uint64_t artifactSequenceEnd = 0;
		if (paramsJson.has_value()) {
			std::string audioArtifactRaw;
			if (blazeclaw::gateway::json::FindRawField(
				paramsJson.value(),
				"audioArtifact",
				audioArtifactRaw) &&
				blazeclaw::gateway::json::IsJsonObjectShape(audioArtifactRaw))
			{
				blazeclaw::gateway::json::FindStringField(
					audioArtifactRaw,
					"handoffMode",
					artifactHandoffMode);
				blazeclaw::gateway::json::FindStringField(
					audioArtifactRaw,
					"streamId",
					artifactStreamId);
				blazeclaw::gateway::json::FindUInt64Field(
					audioArtifactRaw,
					"sequenceStart",
					artifactSequenceStart);
				blazeclaw::gateway::json::FindUInt64Field(
					audioArtifactRaw,
					"sequenceEnd",
					artifactSequenceEnd);
				streamingRequest = (artifactHandoffMode == "pcm_stream");
			}
		}

		// Extract audio path
		std::string audioPath;
		if (paramsJson.has_value()) {
			blazeclaw::gateway::json::FindStringField(paramsJson.value(), "audioPath", audioPath);
		}

		// Determine request type
		const bool previewRequest = runId.rfind("speech-preview-", 0) == 0;
		const bool finalRequest = runId.rfind("speech-final-", 0) == 0;
		const std::string requestType =
			previewRequest ? "preview" : finalRequest ? "final" : "unknown";

		// Trace speech request
		const std::string speechRequestTraceDetail =
			"type=" + requestType +
			" sessionId=" + sessionId +
			" runId=" + runId +
			" streaming=" + std::string(streamingRequest ? "true" : "false") +
			" handoffMode=" + artifactHandoffMode +
			" streamId=" + artifactStreamId +
			" sequenceStart=" + std::to_string(artifactSequenceStart) +
			" sequenceEnd=" + std::to_string(artifactSequenceEnd) +
			" hasAudioPath=" + std::string(audioPath.empty() ? "false" : "true");

		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				L"speech.request.trace",
				speechRequestTraceDetail);
		}

		if (context.traceSpeechBridgeOrder) {
			context.traceSpeechBridgeOrder(
				"transcribe.dispatch",
				"method=speech.transcribe " + speechRequestTraceDetail);
		}

		// Emit speech lifecycle event: queued
		if (context.buildSpeechLifecyclePayloadJson && context.emitSpeechLifecycleEvent) {
			const std::string lifecyclePayload = context.buildSpeechLifecyclePayloadJson(
				"queued",
				sessionId,
				runId,
				audioPath,
				"",
				"",
				0,
				false,
				"",
				"",
				"");
			context.emitSpeechLifecycleEvent(lifecyclePayload);
		}

		// NOTE: The actual speech transcription async completion handling requires posting
		// a window message (kSpeechRpcCompletedMessage) with completion payload and
		// later processing the result in the view's message handler. This extraction
		// provides the structure; the full async flow integration is handled by the
		// main view class through the existing speech RPC completion infrastructure.
		// The router's responsibility is to validate parameters, emit lifecycle events,
		// and delegate to the app's gateway routing.

		// Route to gateway for async processing
		const auto response = context.app->RouteGatewayRequest(request);

		// Post RPC result immediately (for async operations, this may be a pending acknowledgment)
		if (context.buildBridgeRpcResultJson) {
			const std::string responseJson = context.buildBridgeRpcResultJson(response, correlationId);
			context.eventTransport->EmitTopic(BridgeEventTopic::RpcResult, responseJson);
		}

		return true;
	}

	void WebViewSpeechRpcRouter::HandleSpeechRecordingMethod(
		const WebViewRouterContext& context,
		const std::string& method,
		const std::string& correlationId,
		const std::optional<std::string>& paramsJson,
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		if (!response.ok || !response.payloadJson.has_value()) {
			return;
		}

		// Extract session ID
		std::string sessionId = context.bridgeSessionId;
		if (paramsJson.has_value()) {
			std::string extractedSessionId;
			if (blazeclaw::gateway::json::FindStringField(paramsJson.value(), "sessionId", extractedSessionId) &&
				!extractedSessionId.empty()) {
				sessionId = extractedSessionId;
			}
		}

		if (method == "gateway.speech.startRecording") {
			// Extract audio artifact details for tracing
			std::string audioPath;
			blazeclaw::gateway::json::FindStringField(
				response.payloadJson.value(),
				"audioPath",
				audioPath);

			std::string artifactTraceDetail;
			std::string audioArtifactRaw;
			if (blazeclaw::gateway::json::FindRawField(
				response.payloadJson.value(),
				"audioArtifact",
				audioArtifactRaw) &&
				blazeclaw::gateway::json::IsJsonObjectShape(audioArtifactRaw))
			{
				std::string handoffMode;
				std::string streamId;
				std::uint64_t sequenceStart = 0;
				std::uint64_t sequenceEnd = 0;
				blazeclaw::gateway::json::FindStringField(
					audioArtifactRaw,
					"handoffMode",
					handoffMode);
				blazeclaw::gateway::json::FindStringField(
					audioArtifactRaw,
					"streamId",
					streamId);
				blazeclaw::gateway::json::FindUInt64Field(
					audioArtifactRaw,
					"sequenceStart",
					sequenceStart);
				blazeclaw::gateway::json::FindUInt64Field(
					audioArtifactRaw,
					"sequenceEnd",
					sequenceEnd);
				artifactTraceDetail =
					" handoffMode=" + handoffMode +
					" streamId=" + streamId +
					" sequenceStart=" + std::to_string(sequenceStart) +
					" sequenceEnd=" + std::to_string(sequenceEnd);
			}

			if (context.traceSpeechBridgeOrder) {
				context.traceSpeechBridgeOrder(
					"startRecording.complete",
					"method=gateway.speech.startRecording sessionId=" + sessionId +
					" hasAudioPath=" + std::string(audioPath.empty() ? "false" : "true") +
					artifactTraceDetail);
			}

			// Emit speech lifecycle event: recording
			if (context.buildSpeechLifecyclePayloadJson && context.emitSpeechLifecycleEvent) {
				const std::string lifecyclePayload = context.buildSpeechLifecyclePayloadJson(
					"recording",
					sessionId,
					"",
					"",
					"",
					"",
					0,
					false,
					"",
					"",
					"status");
				context.emitSpeechLifecycleEvent(lifecyclePayload);
			}
		}
		else if (method == "gateway.speech.stopRecording") {
			// Similar handling for stopRecording - emit lifecycle event
			if (context.buildSpeechLifecyclePayloadJson && context.emitSpeechLifecycleEvent) {
				const std::string lifecyclePayload = context.buildSpeechLifecyclePayloadJson(
					"stopped",
					sessionId,
					"",
					"",
					"",
					"",
					0,
					false,
					"",
					"",
					"status");
				context.emitSpeechLifecycleEvent(lifecyclePayload);
			}
		}
	}

	void WebViewSpeechRpcRouter::PostRpcErrorResult(
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

	std::string WebViewSpeechRpcRouter::JsonString(const std::string& value)
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
