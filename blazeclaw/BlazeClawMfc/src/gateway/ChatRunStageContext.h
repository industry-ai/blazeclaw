#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	struct ChatRunStageContext {
		std::string requestId;
		std::string method;
		std::optional<std::string> paramsJson;
		std::string runId;
		std::string requestedSessionKey;
		std::string sessionKey;
		std::string message;
		std::string normalizedMessage;
		std::string idempotencyKey;
		bool forceError = false;
		bool hasAttachmentPayload = false;
		bool preferChineseResponse = false;
		std::string runtimeMessage;
		std::uint64_t nowEpochMs = 0;
		std::vector<std::string> stageTrace;
		std::string diagnostics;
	};

} // namespace blazeclaw::gateway
