#include "pch.h"
#include "ChatRuntimeOrchestrationCoordinator.h"

#include "ServiceManager.h"
#include "runtime/ChatRuntimeContracts.h"

namespace blazeclaw::core {

namespace {

using ChatRuntimeRequest = blazeclaw::gateway::GatewayHost::ChatRuntimeRequest;
using ChatRuntimeResult = blazeclaw::gateway::GatewayHost::ChatRuntimeResult;

ChatRuntimeResult MakeCancelledChatResult(const std::string& activeModel) {
	return ChatRuntimeResult{
		.ok = false,
		.assistantText = {},
		.modelId = activeModel,
		.errorCode = runtime::contracts::kErrorCancelled,
		.errorMessage = "chat runtime cancelled",
	};
}

} // namespace

std::optional<blazeclaw::gateway::GatewayHost::ChatRuntimeResult>
ChatRuntimeOrchestrationCoordinator::TryInlineToolInvocation(
	ServiceManager& manager,
	const ChatRuntimeRequest& request,
	const std::string& activeModel,
	const std::optional<std::string>& resolvedSkillInvocationToolTarget) {
	if (const auto inlineInvocationResult =
			manager.TryExecuteInlineToolInvocation(
				request,
				activeModel,
				resolvedSkillInvocationToolTarget);
		inlineInvocationResult.has_value()) {
		return inlineInvocationResult.value();
	}
	return std::nullopt;
}

blazeclaw::gateway::GatewayHost::ChatRuntimeResult
ChatRuntimeOrchestrationCoordinator::RunEmbeddedToolOrchestrationOrProvider(
	ServiceManager& manager,
	const ChatRuntimeRequest& request,
	const std::string& sessionId,
	const std::vector<std::string>& orderedAllowedTargets,
	const std::optional<std::string>& resolvedSkillInvocationToolTarget,
	const std::string& resolvedPromptForRunNarrow,
	const std::string& runtimeMessage,
	const std::string& activeProvider,
	const std::string& activeModel) {
	auto toolBindings = manager.BuildEmbeddedToolBindings();

	const bool canaryEligible = manager.IsEmbeddedDynamicLoopCanaryEligible(
		activeProvider,
		sessionId);
	const bool promotionReady = manager.IsEmbeddedDynamicLoopPromotionReady();
	const bool enableEmbeddedDynamicLoop =
		manager.m_activeConfig.embedded.dynamicToolLoopEnabled &&
		(canaryEligible || promotionReady);
	manager.m_state.embeddedRuntime.lastDynamicLoopEnabled = enableEmbeddedDynamicLoop;
	manager.m_state.embeddedRuntime.lastCanaryEligible = canaryEligible;
	manager.m_state.embeddedRuntime.lastPromotionReady = promotionReady;
	manager.m_state.embeddedRuntime.lastFallbackUsed = false;
	manager.m_state.embeddedRuntime.lastFallbackReason.clear();

	const auto embeddedExecution = manager.m_piEmbeddedService.ExecuteRun(
		EmbeddedRuntimeExecutionRequest{
			.run = EmbeddedRunRequest{
				.sessionId = sessionId,
				.agentId = "default",
				.message = request.message,
			},
			.skillsPrompt = resolvedPromptForRunNarrow,
			.toolBindings = std::move(toolBindings),
			.runtimeTools = manager.m_gatewayHost.ListRuntimeTools(),
			.enforceOrderedAllowlist =
				request.enforceOrderedAllowlist ||
				resolvedSkillInvocationToolTarget.has_value(),
			.orderedAllowedToolTargets = orderedAllowedTargets,
			.enableDynamicToolLoop = enableEmbeddedDynamicLoop,
			.toolExecutorV2 = [&manager](
					   const blazeclaw::gateway::ToolExecuteRequestV2& executeRequest) {
				return manager.m_gatewayHost.ExecuteRuntimeToolV2(executeRequest);
			},
			.toolExecutor = [&manager](
						 const std::string& tool,
						 const std::optional<std::string>& argsJson) {
				return manager.m_gatewayHost.ExecuteRuntimeTool(tool, argsJson);
			},
			.isCancellationRequested = [&manager, runId = request.runId]() {
				return manager.IsEmbeddedRunCancelled(runId);
			},
		});
	manager.ClearEmbeddedRunCancelled(request.runId);

	if (!embeddedExecution.accepted) {
		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = false,
			.assistantText = {},
			.modelId = activeModel,
			.errorCode = "embedded_run_rejected",
			.errorMessage = embeddedExecution.errorMessage.empty()
				? embeddedExecution.reason
				: embeddedExecution.errorMessage,
		};
	}

	if (embeddedExecution.handled) {
		auto runtimeDeltas =
			manager.ConvertEmbeddedTaskDeltas(embeddedExecution.taskDeltas);
		manager.ApplyEmbeddedExecutionTelemetry(embeddedExecution);

		if (!embeddedExecution.success) {
			++manager.m_state.embeddedRuntime.emailFallbackAttemptCount;
			const auto fallbackDecision =
				manager.m_emailFallbackRuntimeCoordinator.EvaluateEmbeddedFailure(
					embeddedExecution.errorCode,
					embeddedExecution.reason);
			if (fallbackDecision.shouldFallback) {
				manager.m_state.embeddedRuntime.lastFallbackUsed = true;
				++manager.m_state.embeddedRuntime.runFallbackCount;
				++manager.m_state.embeddedRuntime.emailFallbackSuccessCount;
				manager.m_state.embeddedRuntime.lastFallbackReason =
					fallbackDecision.fallbackReason;
			}
			else {
				++manager.m_state.embeddedRuntime.emailFallbackFailureCount;
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.assistantDeltas = embeddedExecution.assistantDeltas,
					.taskDeltas = std::move(runtimeDeltas),
					.modelId = activeModel,
					.errorCode = embeddedExecution.errorCode.empty()
						? "embedded_tool_execution_failed"
						: embeddedExecution.errorCode,
					.errorMessage = embeddedExecution.errorMessage.empty()
						? "embedded tool orchestration failed"
						: embeddedExecution.errorMessage,
				};
			}
		}

		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = true,
			.assistantText = embeddedExecution.assistantText,
			.assistantDeltas = embeddedExecution.assistantDeltas,
			.taskDeltas = std::move(runtimeDeltas),
			.modelId = activeModel,
			.errorCode = {},
			.errorMessage = {},
		};
	}

	return manager.ExecuteProviderChatRuntimePath(
		request,
		sessionId,
		runtimeMessage,
		activeProvider,
		activeModel);
}

ChatRuntimeOrchestrationCoordinator::ResolvedSkillsPromptForRun
ChatRuntimeOrchestrationCoordinator::ResolveSkillsPromptForRun(
	ServiceManager& manager) const {
	ResolvedSkillsPromptForRun out;
	out.wide = manager.m_skillsFacade.ResolvePromptForRun(
		&manager.m_skillsRunSnapshot,
		&manager.m_skillsPrompt,
		manager.m_skillsCatalog,
		manager.m_skillsEligibility,
		manager.m_activeConfig,
		std::nullopt,
		manager.m_state.hooks.fallbackPromptInjection,
		manager.m_skillsPromptService);
	out.narrowAscii = WideToNarrowAscii(out.wide);
	return out;
}

blazeclaw::gateway::GatewayHost::ChatRuntimeResult
ChatRuntimeOrchestrationCoordinator::ExecuteChatRuntimeRequestBody(
	ServiceManager& manager,
	const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
	const PreparedChatRequest& prepared,
	const std::string& runtimeMessage,
	const std::string& resolvedPromptForRunNarrow,
	const std::string& activeProvider,
	const std::string& activeModel) const {
	if (manager.IsEmbeddedRunCancelled(request.runId) ||
		manager.IsDeepSeekRunCancelled(request.runId)) {
		return MakeCancelledChatResult(activeModel);
	}

	if (const auto inlineResult = TryInlineToolInvocation(
			manager,
			request,
			activeModel,
			prepared.resolvedSkillInvocationToolTarget)) {
		return *inlineResult;
	}

	return RunEmbeddedToolOrchestrationOrProvider(
		manager,
		request,
		prepared.sessionId,
		prepared.orderedAllowedTargets,
		prepared.resolvedSkillInvocationToolTarget,
		resolvedPromptForRunNarrow,
		runtimeMessage,
		activeProvider,
		activeModel);
}

void ChatRuntimeOrchestrationCoordinator::OnChatRuntimeAborted(
	ServiceManager& manager) const {
	if (!manager.m_localModelActivationEnabled) {
		return;
	}
	if (manager.m_localModelRuntime != nullptr) {
		manager.m_localModelRuntimeSnapshot = manager.m_localModelRuntime->Snapshot();
	}
}

} // namespace blazeclaw::core
