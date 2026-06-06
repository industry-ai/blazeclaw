#include "pch.h"
#include "SpeechBridgeCoordinator.h"
#include "../EventTransport.h"

namespace blazeclaw::speech_bridge {

	void SpeechBridgeCoordinator::HandleSpeechRpcCompletion(
		const SpeechBridgeContext& context,
		SpeechRpcCompletionPayload* payload)
	{
		if (payload == nullptr)
		{
			return;
		}

		// Trace speech transcription completion
		if (context.traceSpeechBridgeOrder)
		{
			context.traceSpeechBridgeOrder(
				"transcribe.complete",
				"method=speech.transcribe ok=" +
				std::string(payload->response.ok ? "true" : "false") +
				" correlationId=" + payload->correlationId +
				" " + payload->requestTraceDetail);
		}

		// Build and emit speech lifecycle event from transcription response
		if (context.buildSpeechLifecyclePayloadFromTranscribeResponse &&
			context.emitSpeechLifecycleEvent)
		{
			const std::string lifecyclePayloadJson =
				context.buildSpeechLifecyclePayloadFromTranscribeResponse(payload->response);
			if (!lifecyclePayloadJson.empty())
			{
				context.emitSpeechLifecycleEvent(lifecyclePayloadJson);
			}
		}

		// Build and emit RPC result event
		if (context.buildBridgeRpcResultJson && context.eventTransport)
		{
			const std::string responseJson = context.buildBridgeRpcResultJson(
				payload->response,
				payload->correlationId);
			context.eventTransport->EmitTopic(BridgeEventTopic::RpcResult, responseJson);
		}

		// Clean up payload
		delete payload;
	}

	void SpeechBridgeCoordinator::HandleSpeechLifecycleDispatch(
		const SpeechBridgeContext& context,
		SpeechLifecycleDispatchPayload* payload)
	{
		if (payload == nullptr)
		{
			return;
		}

		// Emit speech lifecycle event if payload is non-empty
		if (context.emitSpeechLifecycleEvent && !payload->payloadJson.empty())
		{
			context.emitSpeechLifecycleEvent(payload->payloadJson);
		}

		// Clean up payload
		delete payload;
	}

} // namespace blazeclaw::speech_bridge
