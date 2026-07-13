#pragma once

#include "GatewayHost.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blazeclaw::gateway {

class ChatRunPipelineOrchestrator;
class GatewayEventFanoutService;
class GatewayMethodDispatcher;
class GatewaySessionRegistry;
class GatewayToolRegistry;
class GatewayWebSocketTransport;
class TaskDeltaRepository;
class TransportRecipientRegistry;

namespace handlers::runtime {

/// Narrow, route-scoped dependency bundles for `ChatPipelineHandlers::RegisterAll`.
/// Handlers capture only the bundle they need instead of `GatewayHost&`.
struct ChatPipelineRouteDeps {
	struct Dispatch {
		GatewayMethodDispatcher* dispatcher = nullptr;
	};

	struct SessionRegistry {
		GatewaySessionRegistry* sessionRegistry = nullptr;
	};

	struct RuntimeServices {
		GatewayMethodDispatcher* dispatcher = nullptr;
		ChatRunPipelineOrchestrator* chatRunPipeline = nullptr;
		GatewayWebSocketTransport* transport = nullptr;
		GatewayEventFanoutService* eventFanout = nullptr;
		GatewayToolRegistry* toolRegistry = nullptr;
		TransportRecipientRegistry* transportRecipientRegistry = nullptr;
	};

	struct RunTracking {
		std::unordered_map<std::string, GatewayHost::ChatRunState>& runsById;
		std::unordered_map<std::string, std::string>& runByIdempotency;
		std::unordered_map<std::string, GatewayHost::ChatReplayEntry>& replayByIdempotency;
		std::unordered_map<std::string, std::unordered_set<std::string>>& toolEventRecipientsByRun;
		std::unordered_set<std::string>& terminalDeliveredRunIds;
	};

	struct SessionQueues {
		std::unordered_map<std::string, std::deque<GatewayHost::ChatEventState>>& eventsBySession;
		std::unordered_map<std::string, std::vector<std::string>>& historyBySession;
		std::uint64_t& pushEventSeq;
	};

	struct RuntimeCallbacks {
		GatewayHost::ChatRuntimeCallback& chatRuntimeCallback;
		GatewayHost::ChatAbortCallback& chatAbortCallback;
	};

	struct TaskDeltaMetrics {
		TaskDeltaRepository& repository;
		std::size_t& retentionLimit;
		std::uint64_t& runSuccessCount;
		std::uint64_t& runFailureCount;
		std::uint64_t& runTimeoutCount;
		std::uint64_t& runCancelledCount;
		std::uint64_t& runFallbackCount;
		std::function<void()> persist;
	};

	struct PollMetrics {
		std::uint64_t& terminalWaitExceededTotal;
		std::uint64_t& stalledActiveRunTotal;
		std::uint64_t& stalledActiveRunForcedTerminalTotal;
	};

	struct ModelRouting {
		std::string& runtimeDeepSeekApiKey;
		std::string& runtimeDeepSeekDefaultModel;
		std::string& embeddedOrchestrationPath;
		GatewayHost::OrchestrationPathSelectionState& latestOrchestrationPathSelection;
		std::uint64_t& orderedPreflightMissingTargetTotal;
		std::uint64_t& orderedPreflightMissingTargetTerminalEmittedTotal;
	};

	struct SkillsCatalog {
		SkillsCatalogGatewayState& catalogState;
		GatewayHost::SkillsRefreshCallback& refreshCallback;
		GatewayHost::SkillsUpdateCallback& updateCallback;
	};

	struct ConfigSchema {
		GatewayHost::ConfigSchemaGetCallback& getCallback;
		GatewayHost::ConfigSchemaLookupCallback& lookupCallback;
	};

	Dispatch dispatch{};
	SessionRegistry session{};
	RuntimeServices runtime{};
	RunTracking runTracking;
	SessionQueues sessionQueues;
	RuntimeCallbacks callbacks;
	TaskDeltaMetrics taskDeltas;
	PollMetrics pollMetrics;
	ModelRouting modelRouting;
	SkillsCatalog skills;
	ConfigSchema configSchema;

	[[nodiscard]] static ChatPipelineRouteDeps Bind(GatewayHost& host);
};

} // namespace handlers::runtime

} // namespace blazeclaw::gateway
