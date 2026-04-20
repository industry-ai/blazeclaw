#include "pch.h"
#include "GatewayHostBindingCoordinator.h"

#include "EmbeddingsService.h"
#include "ServiceManager.h"

#include <string>
#include <vector>
#include <Windows.h>

namespace blazeclaw::core {
namespace {

	std::wstring Utf8ToWide(const std::string& value) {
		if (value.empty()) {
			return {};
		}

		const int needed = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (needed <= 0) {
			return {};
		}

		std::wstring output(static_cast<std::size_t>(needed), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			needed);
		return output;
	}

	std::string WideConfigPathToUtf8Lossy(const std::wstring& value) {
		std::string output;
		output.reserve(value.size());
		for (const wchar_t ch : value) {
			output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
		}
		return output;
	}

} // namespace

void GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks(ServiceManager& manager) {
	RegisterSkillsRelatedCallbacks(manager);
	BindGatewayPolicyCallbacks(manager);
	manager.BindToolRuntimeCallbacks();
	RegisterChatRuntimeCallbacks(manager);
	BindEmbeddingsCallbacks(manager);
}

void GatewayHostBindingCoordinator::BindGatewayPolicyCallbacks(ServiceManager& manager) {
	manager.m_gatewayHost.SetEmbeddedOrchestrationPath(
		WideConfigPathToUtf8Lossy(manager.m_activeConfig.embedded.orchestrationPath));
	const auto gatewayEmailBinding =
		manager.m_emailPolicyOrchestrationService.BuildGatewayPolicyBinding(
			manager.m_activeConfig.email,
			manager.m_state.emailPolicy.runtimeEnabled,
			manager.m_state.emailPolicy.runtimeEnforce,
			manager.m_emailFallbackResolvedPolicy);
	manager.m_gatewayHost.SetEmailFallbackRuntimeFlags(
		gatewayEmailBinding.preflightEnabled,
		gatewayEmailBinding.runtimeEnabled,
		gatewayEmailBinding.runtimeEnforce);
	manager.m_gatewayHost.SetEmailFallbackResolvedPolicy(
		gatewayEmailBinding.backends,
		gatewayEmailBinding.onUnavailable,
		gatewayEmailBinding.onAuthError,
		gatewayEmailBinding.onExecError,
		gatewayEmailBinding.retryMaxAttempts,
		gatewayEmailBinding.retryDelayMs,
		gatewayEmailBinding.requiresApproval,
		gatewayEmailBinding.approvalTokenTtlMinutes,
		gatewayEmailBinding.profileId);
}

void GatewayHostBindingCoordinator::BindEmbeddingsCallbacks(ServiceManager& manager) {
	manager.m_gatewayHost.SetEmbeddingsGenerateCallback([&manager](
		const blazeclaw::gateway::GatewayHost::EmbeddingsGenerateRequest& request) {
		const auto result = manager.m_embeddingsService.EmbedText(
			EmbeddingRequest{
				.text = Utf8ToWide(request.text),
				.normalize = request.normalize,
				.traceId = request.traceId,
			});

		blazeclaw::gateway::GatewayHost::EmbeddingsGenerateResult gatewayResult;
		gatewayResult.ok = result.ok;
		gatewayResult.vector = result.vector;
		gatewayResult.dimension = result.dimension;
		gatewayResult.provider = result.provider;
		gatewayResult.modelId = result.modelId;
		gatewayResult.latencyMs = result.latencyMs;
		gatewayResult.status = manager.m_embeddingsService.Snapshot().status;

		if (result.error.has_value()) {
			gatewayResult.errorCode =
				EmbeddingErrorCodeToString(result.error->code);
			gatewayResult.errorMessage = result.error->message;
		}

		return gatewayResult;
	});

	manager.m_gatewayHost.SetEmbeddingsBatchCallback([&manager](
		const blazeclaw::gateway::GatewayHost::EmbeddingsBatchRequest& request) {
		std::vector<std::wstring> texts;
		texts.reserve(request.texts.size());
		for (const auto& text : request.texts) {
			texts.push_back(Utf8ToWide(text));
		}

		const auto result = manager.m_embeddingsService.EmbedBatch(
			EmbeddingBatchRequest{
				.texts = std::move(texts),
				.normalize = request.normalize,
				.traceId = request.traceId,
			});

		blazeclaw::gateway::GatewayHost::EmbeddingsBatchResult gatewayResult;
		gatewayResult.ok = result.ok;
		gatewayResult.vectors = result.vectors;
		gatewayResult.dimension = result.dimension;
		gatewayResult.provider = result.provider;
		gatewayResult.modelId = result.modelId;
		gatewayResult.latencyMs = result.latencyMs;
		gatewayResult.status = manager.m_embeddingsService.Snapshot().status;

		if (result.error.has_value()) {
			gatewayResult.errorCode =
				EmbeddingErrorCodeToString(result.error->code);
			gatewayResult.errorMessage = result.error->message;
		}

		return gatewayResult;
	});
}

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
