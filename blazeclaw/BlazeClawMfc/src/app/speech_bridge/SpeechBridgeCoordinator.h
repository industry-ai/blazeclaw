#pragma once

#include <string>

#include "SpeechBridgeContext.h"
#include "../../gateway/GatewayProtocolModels.h"

namespace blazeclaw::speech_bridge {

	/// <summary>
	/// Payload for speech RPC completion events (posted via WM_APP message).
	/// Matches the structure previously defined in BlazeClawMFCView.cpp.
	/// </summary>
	struct SpeechRpcCompletionPayload
	{
		std::string correlationId;
		std::string sessionId;
		std::string runId;
		std::string requestType;
		std::string requestTraceDetail;
		blazeclaw::gateway::protocol::ResponseFrame response;
	};

	/// <summary>
	/// Payload for speech lifecycle dispatch events (posted via WM_APP message).
	/// Matches the structure previously defined in BlazeClawMFCView.cpp.
	/// </summary>
	struct SpeechLifecycleDispatchPayload
	{
		std::string payloadJson;
	};

	/// <summary>
	/// Coordinator for speech bridge orchestration.
	/// Handles async speech RPC completion, lifecycle event emission,
	/// and lifecycle dedupe logic extracted from CBlazeClawMFCView.
	/// </summary>
	class SpeechBridgeCoordinator
	{
	public:
		/// <summary>
		/// Handle speech RPC completion (called from view's WM_APP handler).
		/// Processes the completion payload, emits lifecycle events if needed,
		/// posts RPC result events, and cleans up payload memory.
		/// </summary>
		/// <param name="context">Shared dependencies for speech orchestration</param>
		/// <param name="payload">Speech RPC completion payload (caller must manage lifetime)</param>
		static void HandleSpeechRpcCompletion(
			const SpeechBridgeContext& context,
			SpeechRpcCompletionPayload* payload);

		/// <summary>
		/// Handle speech lifecycle dispatch (called from view's WM_APP handler).
		/// Emits the lifecycle event payload and cleans up payload memory.
		/// </summary>
		/// <param name="context">Shared dependencies for speech orchestration</param>
		/// <param name="payload">Speech lifecycle dispatch payload (caller must manage lifetime)</param>
		static void HandleSpeechLifecycleDispatch(
			const SpeechBridgeContext& context,
			SpeechLifecycleDispatchPayload* payload);
	};

} // namespace blazeclaw::speech_bridge
