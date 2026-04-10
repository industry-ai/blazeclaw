#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	struct ChatRunStageContext {
		std::string requestId;
		std::string method;
		std::string runId;
		std::string sessionKey;
		std::vector<std::string> stageTrace;
		std::string diagnostics;
	};

} // namespace blazeclaw::gateway
