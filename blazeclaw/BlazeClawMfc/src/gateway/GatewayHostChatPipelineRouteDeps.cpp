#include "pch.h"
#include "GatewayHostChatPipelineRouteDeps.h"

#include "GatewayRuntimeContext.h"
#include "TaskDeltaRepository.h"

namespace blazeclaw::gateway::handlers::runtime {

ChatPipelineRouteDeps ChatPipelineRouteDeps::Bind(GatewayHost& host) {
	const GatewayRuntimeContext& runtime = host.RuntimeContext();

	ChatPipelineRouteDeps deps{
		.dispatch = {
			.dispatcher = runtime.dispatcher,
		},
		.session = {
			.sessionRegistry = runtime.sessionRegistry,
		},
		.runtime = {
			.dispatcher = runtime.dispatcher,
			.chatRunPipeline = runtime.chatRunPipeline,
			.transport = runtime.transport,
			.eventFanout = runtime.eventFanout,
			.toolRegistry = runtime.toolRegistry,
			.transportRecipientRegistry = runtime.transportRecipientRegistry,
		},
		.runTracking = {
			.runsById = host.m_chatRunsById,
			.runByIdempotency = host.m_chatRunByIdempotency,
			.replayByIdempotency = host.m_chatReplayByIdempotency,
			.toolEventRecipientsByRun = host.m_chatToolEventRecipientsByRun,
			.terminalDeliveredRunIds = host.m_chatTerminalDeliveredRunIds,
		},
		.sessionQueues = {
			.eventsBySession = host.m_chatEventsBySession,
			.historyBySession = host.m_chatHistoryBySession,
			.pushEventSeq = host.m_chatPushEventSeq,
		},
		.callbacks = {
			.chatRuntimeCallback = host.m_chatRuntimeCallback,
			.chatAbortCallback = host.m_chatAbortCallback,
		},
		.taskDeltas = {
			.repository = host.m_taskDeltaRepository,
			.retentionLimit = host.m_taskDeltasRetentionLimit,
			.runSuccessCount = host.m_taskDeltaRunSuccessCount,
			.runFailureCount = host.m_taskDeltaRunFailureCount,
			.runTimeoutCount = host.m_taskDeltaRunTimeoutCount,
			.runCancelledCount = host.m_taskDeltaRunCancelledCount,
			.runFallbackCount = host.m_taskDeltaRunFallbackCount,
			.persist = [&host]() { host.PersistTaskDeltas(); },
		},
		.pollMetrics = {
			.terminalWaitExceededTotal = host.m_chatPollTerminalWaitExceededTotal,
			.stalledActiveRunTotal = host.m_chatPollStalledActiveRunTotal,
			.stalledActiveRunForcedTerminalTotal =
				host.m_chatPollStalledActiveRunForcedTerminalTotal,
		},
		.modelRouting = {
			.runtimeDeepSeekApiKey = host.m_runtimeDeepSeekApiKey,
			.runtimeDeepSeekDefaultModel = host.m_runtimeDeepSeekDefaultModel,
			.embeddedOrchestrationPath = host.m_embeddedOrchestrationPath,
			.latestOrchestrationPathSelection = host.m_latestOrchestrationPathSelection,
			.orderedPreflightMissingTargetTotal = host.m_orderedPreflightMissingTargetTotal,
			.orderedPreflightMissingTargetTerminalEmittedTotal =
				host.m_orderedPreflightMissingTargetTerminalEmittedTotal,
		},
		.skills = {
			.catalogState = host.m_skillsCatalogState,
			.refreshCallback = host.m_skillsRefreshCallback,
			.updateCallback = host.m_skillsUpdateCallback,
		},
		.configSchema = {
			.getCallback = host.m_configSchemaGetCallback,
			.lookupCallback = host.m_configSchemaLookupCallback,
		},
	};

	return deps;
}

} // namespace blazeclaw::gateway::handlers::runtime
