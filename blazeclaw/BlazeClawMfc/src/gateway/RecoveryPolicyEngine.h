#pragma once

#include "FailureClassifier.h"
#include "RunLoopBudget.h"
#include "CompactionCoordinator.h"
#include "ToolResultTruncationCoordinator.h"
#include "AuthProfilePolicyService.h"
#include "ContextEnginePolicySelector.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

	struct RecoveryOutcome {
		bool recovered = false;
		bool shouldRetry = false;
		bool shouldReinvokeRuntime = false;
		bool compactionApplied = false;
		bool truncationApplied = false;
		std::string selectedProfileId = "default";
		std::string selectedContextEngineId = "default";
		std::string terminalErrorCode;
		std::string terminalErrorMessage;
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> recoveryDeltas;
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> normalizedDeltas;
	};

	struct RecoveryRequest {
		std::string runId;
		std::string sessionKey;
		std::string message;
		std::string errorCode;
		std::string errorMessage;
		std::string authProfileId = "default";
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> taskDeltas;
	};

	class RecoveryPolicyEngine {
	public:
		[[nodiscard]] static RecoveryOutcome Execute(
			const RecoveryRequest& request,
			RunLoopBudget& budget);
	};

} // namespace blazeclaw::gateway
