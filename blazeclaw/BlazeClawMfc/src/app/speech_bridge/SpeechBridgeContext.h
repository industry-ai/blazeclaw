#pragma once

#include <functional>
#include <optional>
#include <string>

#include "../../gateway/GatewayProtocolModels.h"

// Forward declarations
class CEventTransport;

namespace blazeclaw::speech_bridge {

	/// <summary>
	/// Shared dependency carrier for speech bridge orchestration.
	/// Provides callbacks and dependencies needed by SpeechBridgeCoordinator
	/// to handle async speech RPC completion and lifecycle event emission
	/// without direct coupling to the view class.
	/// </summary>
	struct SpeechBridgeContext
	{
		// Core dependencies
		CEventTransport* eventTransport = nullptr;

		// Diagnostics callbacks
		std::function<void(const std::wstring& marker, const std::string& detail)> appendChatProcedureStatusLineWithDetail;
		std::function<void(const std::string& marker, const std::string& detail)> traceSpeechBridgeOrder;

		// Speech lifecycle helpers
		std::function<std::string(
			const blazeclaw::gateway::protocol::ResponseFrame& response)> buildSpeechLifecyclePayloadFromTranscribeResponse;
		std::function<void(const std::string& lifecyclePayloadJson)> emitSpeechLifecycleEvent;

		// Bridge RPC response builder
		std::function<std::string(
			const blazeclaw::gateway::protocol::ResponseFrame& response,
			const std::string& correlationId)> buildBridgeRpcResultJson;
	};

} // namespace blazeclaw::speech_bridge
