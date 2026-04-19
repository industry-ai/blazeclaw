#include "pch.h"
#include "GatewayHostBindingCoordinator.h"

#include "ServiceManager.h"

#include <string>

namespace blazeclaw::core {

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
		auto& orchestrator = manager.m_chatRuntimeOrchestrationCoordinator;
		manager.m_gatewayHost.SetChatRuntimeCallback([&manager, &orchestrator](
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
				const auto preparedChatRequest =
					orchestrator.PrepareChatRequest(
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

				const auto resolvedPrompt = orchestrator.ResolveSkillsPromptForRun(manager);
				const std::string runtimeMessage =
					ChatRuntimeOrchestrationCoordinator::BuildSkillsInjectedMessage(
						preparedChatRequest.inboundMessageForAgent,
						resolvedPrompt.wide,
						static_cast<std::size_t>(
							manager.m_activeConfig.skills.limits.maxSkillsPromptChars));
				const std::string activeProvider = manager.m_activeChatProvider;
				const std::string activeModel = manager.m_activeChatModel;
				auto executeRequest = [&manager,
					&orchestrator,
					request,
					preparedChatRequest,
					runtimeMessage,
					resolvedPrompt,
					activeProvider,
					activeModel]() -> blazeclaw::gateway::GatewayHost::ChatRuntimeResult {
					return orchestrator.ExecuteChatRuntimeRequestBody(
						manager,
						request,
						preparedChatRequest,
						runtimeMessage,
						resolvedPrompt.narrowAscii,
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
				manager.m_chatRuntimeOrchestrationCoordinator.OnChatRuntimeAborted(manager);
				return cancelled;
			});

}

} // namespace blazeclaw::core
