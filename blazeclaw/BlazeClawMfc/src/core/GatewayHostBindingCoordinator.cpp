#include "pch.h"
#include "GatewayHostBindingCoordinator.h"

#include "ServiceManager.h"

#include <string>

namespace blazeclaw::core {

namespace {

std::string WideToNarrowAsciiLocal(const std::wstring& value) {
	std::string output;
	output.reserve(value.size());
	for (const auto ch : value) {
		output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
	}
	return output;
}

} // namespace

void GatewayHostBindingCoordinator::RegisterSkillsRelatedCallbacks(ServiceManager& manager) {
		manager.RefreshGatewaySkillsStateProjection();
		manager.PublishGatewaySkillsStateProjection();
		manager.m_gatewayHost.SetConfigSchemaGetCallback([&manager]() {
			return manager.BuildConfigSchemaGatewayState();
			});
		manager.m_gatewayHost.SetConfigSchemaLookupCallback([&manager](
			const std::string& path) {
				return manager.LookupConfigSchemaGatewayPath(path);
			});
		manager.m_gatewayHost.SetSkillsRefreshCallback([&manager]() {
			manager.RefreshSkillsState(manager.m_activeConfig, true, L"manual-refresh");
			return manager.m_gatewaySkillsStateProjection;
			});
		// Single parse/validate/response path for skills.update (see SkillsGatewayMethodHandler).
		manager.m_gatewayHost.SetSkillsUpdateCallback([&manager](
			const blazeclaw::gateway::protocol::RequestFrame& request) {
				return manager.m_skillsGatewayMethodHandler.HandleSkillsUpdate(
					request,
					SkillsGatewayMethodHandler::Dependencies{
						.persistSkillConfigEnv =
							manager.m_skillsHostCallbacks.persistSkillConfigEnv,
						.refreshSkillView = manager.m_skillsHostCallbacks.refreshSkillView,
						.refreshSkillsState = [&manager]() {
							manager.RefreshSkillsState(manager.m_activeConfig, true, L"skills.update");
						},
						.publishGatewaySkillsStateProjection = [&manager]() {
							manager.PublishGatewaySkillsStateProjection();
						},
					});
			});

}

void GatewayHostBindingCoordinator::RegisterChatRuntimeCallbacks(ServiceManager& manager) {
		manager.m_gatewayHost.SetChatRuntimeCallback([&manager](
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
				const auto preparedChatRequest =
					manager.m_chatRuntimeOrchestrationCoordinator.PrepareChatRequest(
						request,
						[&manager](
							const bool allowTextCommands,
							const std::string& commandBodyNormalized) {
								return manager.ShouldLoadSkillCommandsForInlineActions(
									allowTextCommands,
									commandBodyNormalized);
						},
						[&manager](const std::string& commandBodyNormalized) {
							return manager.ResolveSkillInvocationToolTarget(commandBodyNormalized);
						},
						[&manager](const std::string& commandBodyNormalized) {
							return manager.ResolveSkillInvocationPromptRewrite(commandBodyNormalized);
						},
						[&manager](
							const std::vector<std::string>& requestedTargets,
							const std::optional<std::string>& resolvedTarget) {
								return manager.BuildOrderedAllowedToolTargets(
									requestedTargets,
									resolvedTarget);
						});

				if (!preparedChatRequest.shouldLoadInlineSkillCommands) {
					TRACE(
						"[InlineActions] slash gate skipped skill command load for message: %s\n",
						preparedChatRequest.commandBodyForInline.c_str());
				}

				const std::wstring resolvedPromptForRunWide =
					manager.m_skillsFacade.ResolvePromptForRun(
						&manager.m_skillsRunSnapshot,
						&manager.m_skillsPrompt,
						manager.m_skillsCatalog,
						manager.m_skillsEligibility,
						manager.m_activeConfig,
						std::nullopt,
						manager.m_state.hooks.fallbackPromptInjection,
						manager.m_skillsPromptService);
				const std::wstring resolvedPromptForRun =
					resolvedPromptForRunWide;
				const std::string resolvedPromptForRunNarrow =
					WideToNarrowAsciiLocal(resolvedPromptForRun);
				const std::string runtimeMessage =
					ChatRuntimeOrchestrationCoordinator::BuildSkillsInjectedMessage(
						preparedChatRequest.inboundMessageForAgent,
						resolvedPromptForRun,
						static_cast<std::size_t>(
							manager.m_activeConfig.skills.limits.maxSkillsPromptChars));
				const std::string activeProvider = manager.m_activeChatProvider;
				const std::string activeModel = manager.m_activeChatModel;
				auto executeRequest = [&manager,
					request,
					sessionId = preparedChatRequest.sessionId,
					orderedAllowedTargets =
					preparedChatRequest.orderedAllowedTargets,
					resolvedSkillInvocationToolTarget =
					preparedChatRequest.resolvedSkillInvocationToolTarget,
					runtimeMessage,
					resolvedPromptForRunNarrow,
					activeProvider,
					activeModel]() -> blazeclaw::gateway::GatewayHost::ChatRuntimeResult {
					if (manager.IsEmbeddedRunCancelled(request.runId) ||
						manager.IsDeepSeekRunCancelled(request.runId)) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = activeModel,
							.errorCode = runtime::contracts::kErrorCancelled,
							.errorMessage = "chat runtime cancelled",
						};
					}

					if (const auto inlineInvocationResult =
						manager.TryExecuteInlineToolInvocation(
							request,
							activeModel,
							resolvedSkillInvocationToolTarget);
						inlineInvocationResult.has_value()) {
						return inlineInvocationResult.value();
					}

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
				};

				return manager.m_chatRuntime.Execute(
					CChatRuntime::RuntimeExecutionRequest{
						.request = request,
						.sessionId = preparedChatRequest.sessionId,
						.runtimeMessage = runtimeMessage,
						.provider = activeProvider,
						.model = activeModel,
						.execute = std::move(executeRequest),
					});
			});

		manager.m_gatewayHost.SetChatAbortCallback([&manager](
			const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request) {
				const bool cancelled = manager.m_chatRuntime.Abort(request);
				if (manager.m_localModelActivationEnabled)
				{
					if (manager.m_localModelRuntime != nullptr) {
						manager.m_localModelRuntimeSnapshot = manager.m_localModelRuntime->Snapshot();
					}
				}
				return cancelled;
			});

}

} // namespace blazeclaw::core

