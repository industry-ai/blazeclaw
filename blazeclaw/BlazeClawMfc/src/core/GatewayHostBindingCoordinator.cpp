#include "pch.h"
#include "GatewayHostBindingCoordinator.h"

#include "EmbeddingsService.h"
#include "ServiceManager.h"
#include "../app/CMgrMessage.h"
#include "../gateway/Telemetry.h"

#include <algorithm>
#include <afxstr.h>
#include <cctype>
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

		bool ContainsAnyFragment(
			const std::string& text,
			std::initializer_list<const char*> fragments) {
			for (const auto* fragment : fragments) {
				if (fragment == nullptr || *fragment == '\0') {
					continue;
				}
				if (text.find(fragment) != std::string::npos) {
					return true;
				}
			}
			return false;
		}

		bool ContainsAnyWideFragment(
			const std::wstring& text,
			std::initializer_list<const wchar_t*> fragments) {
			for (const auto* fragment : fragments) {
				if (fragment == nullptr || *fragment == L'\0') {
					continue;
				}
				if (text.find(fragment) != std::wstring::npos) {
					return true;
				}
			}
			return false;
		}

		std::string ToLowerAscii(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		bool LooksLikeInboxIntentAnyLanguage(const std::string& message) {
			const std::string lower = ToLowerAscii(message);
			const std::wstring wide = Utf8ToWide(message);
			const bool inboxSignal = ContainsAnyFragment(
				lower,
				{ "inbox", "mailbox", "email", "mail", "unread" }) ||
				ContainsAnyWideFragment(
					wide,
					{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" });
			const bool replySignal = ContainsAnyFragment(
				lower,
				{ "reply", "respond", "needs a reply", "need a reply" }) ||
				ContainsAnyWideFragment(
					wide,
					{ L"回复", L"回信", L"需要回复", L"尽快回复" });
			return inboxSignal && replySignal;
		}

		std::string DetectRoutingLanguage(const std::string& message) {
			const std::wstring wide = Utf8ToWide(message);
			if (ContainsAnyWideFragment(
				wide,
				{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"回复", L"回信" })) {
				return "zh";
			}

			bool hasNonAscii = false;
			for (const unsigned char ch : message) {
				if (ch > 0x7F) {
					hasNonAscii = true;
					break;
				}
			}
			if (hasNonAscii) {
				return "non_ascii";
			}
			return "en";
		}

	} // namespace

	void GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks(ServiceManager& manager) {
		RegisterSkillsRelatedCallbacks(manager);
		BindGatewayPolicyCallbacks(manager);
		manager.BindToolRuntimeCallbacks();
		RegisterChatRuntimeCallbacks(manager);
		BindEmbeddingsCallbacks(manager);
		manager.m_gatewayHost.SetParityLifecycleExportCallback(
			[&manager]() {
				return manager.BuildGatewayParityLifecycleTraceJson();
			});
	}

	void GatewayHostBindingCoordinator::BindGatewayPolicyCallbacks(ServiceManager& manager) {
		manager.m_gatewayHost.SetEmbeddedOrchestrationPath(
			WideConfigPathToUtf8Lossy(manager.m_activeConfig.embedded.orchestrationPath));
		manager.m_gatewayHost.SetNodeParityRuntimeFlags(
			manager.m_activeConfig.embedded.nodeParityEnabled,
			manager.m_activeConfig.embedded.nodeParityDiagnosticsEnabled,
			WideConfigPathToUtf8Lossy(manager.m_activeConfig.embedded.nodeParityRolloutMode));
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

		manager.m_gatewayHost.SetSpeechExecutionUpdateCallback([&manager](
			const speechrecognition::SpeechExecutionState& state) {
				UNREFERENCED_PARAMETER(state);
			});

		manager.m_gatewayHost.SetSpeechTranscribeAcceptedCallback([&manager](
			const blazeclaw::gateway::GatewayHost::SpeechExecutionRequest& request) {
				const auto accepted = manager.m_speechTranscriptionCoordinator.Accept(
					speechrecognition::SpeechExecutionRequest{
						.runId = request.runId,
						.sessionId = request.sessionId,
						.audioPath = request.audioPath,
						.audioArtifact = request.audioArtifact,
						.language = request.language,
						.prompt = request.prompt,
					});

				blazeclaw::gateway::GatewayHost::SpeechExecutionAccepted gatewayAccepted;
				gatewayAccepted.accepted = accepted.accepted;
				gatewayAccepted.executionState = accepted.executionState;
				if (accepted.error.has_value()) {
					gatewayAccepted.errorCode =
						speechrecognition::SpeechRecognitionErrorCodeToString(accepted.error->code);
					gatewayAccepted.errorMessage = accepted.error->message;
				}

				return gatewayAccepted;
			});

		manager.m_gatewayHost.SetSpeechExecutionStatusCallback([&manager](
			const std::string& runId) {
				const auto status = manager.m_speechTranscriptionCoordinator.GetStatus(runId);
				blazeclaw::gateway::GatewayHost::SpeechExecutionStatus gatewayStatus;
				gatewayStatus.found = status.found;
				gatewayStatus.executionState = status.executionState;
				return gatewayStatus;
			});

		manager.m_gatewayHost.SetSpeechCancelCallback([&manager](
			const std::string& runId) {
				return manager.m_speechTranscriptionCoordinator.Cancel(
					manager.m_speechRecognitionRuntime,
					runId);
			});

		manager.m_gatewayHost.SetSpeechTranscribeCallback([&manager](
			const blazeclaw::gateway::GatewayHost::SpeechTranscribeRequest& request) {
				const auto result = manager.m_speechTranscriptionCoordinator.Execute(
					manager.m_speechRecognitionRuntime,
					speechrecognition::SpeechExecutionRequest{
						.runId = request.runId,
						.sessionId = request.sessionId,
						.audioPath = request.audioPath,
						.audioArtifact = request.audioArtifact,
						.language = request.language,
						.prompt = request.prompt,
					});
				manager.m_speechRecognition = manager.m_speechRecognitionRuntime.Snapshot();

				blazeclaw::gateway::GatewayHost::SpeechTranscribeResult gatewayResult;
				gatewayResult.ok = result.ok;
				gatewayResult.cancelled = result.cancelled;
				gatewayResult.text = result.text;
				gatewayResult.language = result.language;
				gatewayResult.latencyMs = result.latencyMs;
				gatewayResult.sessionState = result.sessionState;
				if (result.error.has_value()) {
					gatewayResult.errorCode =
						speechrecognition::SpeechRecognitionErrorCodeToString(result.error->code);
					gatewayResult.errorMessage = result.error->message;
				}

				return gatewayResult;
			});

		manager.m_gatewayHost.SetSpeechStatusCallback([&manager]() {
			auto errorCodeToString = [](texttospeech::TextToSpeechErrorCode code) {
				switch (code) {
				case texttospeech::TextToSpeechErrorCode::None: return std::string("none");
				case texttospeech::TextToSpeechErrorCode::TextToSpeechDisabled: return std::string("text_to_speech_disabled");
				case texttospeech::TextToSpeechErrorCode::ProviderNotSupported: return std::string("provider_not_supported");
				case texttospeech::TextToSpeechErrorCode::ModelNotFound: return std::string("model_not_found");
				case texttospeech::TextToSpeechErrorCode::InvalidInput: return std::string("invalid_input");
				case texttospeech::TextToSpeechErrorCode::RuntimeUnavailable: return std::string("runtime_unavailable");
				case texttospeech::TextToSpeechErrorCode::Cancelled: return std::string("cancelled");
				default: return std::string("unknown");
				}
			};

			blazeclaw::gateway::GatewayHost::SpeechStatusResult status;
			status.supported = true;
			status.ready = manager.IsRunning();
			status.speaking = manager.m_textToSpeech.speaking;
			status.utteranceId = manager.m_textToSpeech.activeUtteranceId;
			status.provider = manager.m_textToSpeech.provider;
			status.model = manager.m_textToSpeech.model;
			status.voice = manager.m_textToSpeech.voice;
			status.status = manager.m_textToSpeech.status;
			if (manager.m_textToSpeech.error.has_value()) {
				status.errorCode = errorCodeToString(manager.m_textToSpeech.error->code);
				status.errorMessage = manager.m_textToSpeech.error->message;
			}
			return status;
			});

		manager.m_gatewayHost.SetSpeechRecognitionRuntimeStatusCallback([&manager]() {
			const auto snapshot = manager.m_speechRecognitionRuntime.Snapshot();
			manager.m_speechRecognition = snapshot;

			blazeclaw::gateway::GatewayHost::SpeechRecognitionRuntimeStatus status;
			status.enabled = snapshot.enabled;
			status.ready = snapshot.ready;
			status.status = snapshot.status;
			status.provider = snapshot.provider;
			status.modelPath = snapshot.modelPath;
			status.modelLayout = snapshot.modelLayout;
			status.modelVariant = snapshot.modelVariant;
			status.runtimeHotMode = snapshot.runtimeHotMode;
			status.runtimeHotLifecycleState = snapshot.runtimeHotLifecycleState;
			status.runtimeHotWarmupEnabled = snapshot.runtimeHotWarmupEnabled;
			status.runtimeHotWarmupRuns = snapshot.runtimeHotWarmupRuns;
			status.runtimeHotIdleTimeoutMs = snapshot.runtimeHotIdleTimeoutMs;
			status.hotwordsEnabled = snapshot.hotwordsEnabled;
			status.hotwordsCount = snapshot.hotwordsCount;
			status.hotwordsMaxCount = snapshot.hotwordsMaxCount;
			status.hotwordsApplyStage = snapshot.hotwordsApplyStage;
			status.hotwordsDebugDumpPrompt = snapshot.hotwordsDebugDumpPrompt;
			status.lastPromptBuildStatus = snapshot.lastPromptBuildStatus;
			status.lastPromptBuildError = snapshot.lastPromptBuildError;
			status.effectiveExecutionProvider = snapshot.effectiveExecutionProvider;
			return status;
			});

		manager.m_gatewayHost.SetSpeechSpeakCallback([&manager](
			const blazeclaw::gateway::GatewayHost::SpeechSpeakRequest& request) {
			manager.m_textToSpeech.speakRequestsStarted += 1;
			manager.m_textToSpeech.enabled = true;
			manager.m_textToSpeech.ready = manager.IsRunning();
			manager.m_textToSpeech.provider = request.provider.empty() ? "default" : request.provider;
			manager.m_textToSpeech.model = request.model.empty() ? "default" : request.model;
			manager.m_textToSpeech.voice = request.voice.empty() ? "default" : request.voice;
			manager.m_textToSpeech.activeUtteranceId =
				request.runId.empty()
				? std::string("utterance-") + std::to_string(manager.m_textToSpeech.speakRequestsStarted)
				: request.runId + "-" + std::to_string(manager.m_textToSpeech.speakRequestsStarted);
			manager.m_textToSpeech.speaking = true;
			manager.m_textToSpeech.status = "speaking";
			manager.m_textToSpeech.error.reset();
			manager.m_textToSpeech.speakRequestsCompleted += 1;

			blazeclaw::gateway::GatewayHost::SpeechSpeakResult result;
			result.ok = true;
			result.cancelled = false;
			result.speaking = manager.m_textToSpeech.speaking;
			result.utteranceId = manager.m_textToSpeech.activeUtteranceId;
			result.normalizedText = request.text;
			result.audioPath =
				"artifacts/tts/" + manager.m_textToSpeech.activeUtteranceId + ".wav";
			result.voice = manager.m_textToSpeech.voice;
			result.provider = manager.m_textToSpeech.provider;
			result.model = manager.m_textToSpeech.model;
			result.latencyMs = 0;
			result.status = manager.m_textToSpeech.status;
			return result;
			});

		manager.m_gatewayHost.SetSpeechStopCallback([&manager](
			const blazeclaw::gateway::GatewayHost::SpeechStopRequest& request) {
			manager.m_textToSpeech.stopRequests += 1;
			manager.m_textToSpeech.speaking = false;
			manager.m_textToSpeech.status = "stopped";
			if (!request.utteranceId.empty()) {
				manager.m_textToSpeech.activeUtteranceId = request.utteranceId;
			}

			blazeclaw::gateway::GatewayHost::SpeechStopResult result;
			result.ok = true;
			result.stopped = true;
			result.utteranceId = manager.m_textToSpeech.activeUtteranceId;
			result.status = manager.m_textToSpeech.status;
			return result;
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
			manager.PublishGatewaySkillsStateProjection();
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
				const std::string requestProviderOverride =
					ToLowerAscii(request.providerOverride);
				const std::string requestModelOverride =
					request.modelIdOverride;
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
				const std::string detectedLanguage =
					DetectRoutingLanguage(preparedChatRequest.commandBodyForInline);
				const std::string normalizedIntent =
					LooksLikeInboxIntentAnyLanguage(preparedChatRequest.commandBodyForInline)
					? "inbox_triage"
					: "unknown";
				const std::optional<std::string> routingFallbackReason =
					(normalizedIntent == "inbox_triage" &&
						!preparedChatRequest.resolvedSkillInvocationToolTarget.has_value())
					? std::optional<std::string>("intent_not_matched")
					: std::nullopt;
				blazeclaw::gateway::EmitTelemetryEvent(
					"gateway.chat.routing.decision",
					std::string("{\"runId\":") +
					blazeclaw::gateway::JsonString(request.runId) +
					",\"sessionKey\":" +
					blazeclaw::gateway::JsonString(request.sessionKey) +
					",\"inlineSkillCommands\":" +
					std::string(preparedChatRequest.shouldLoadInlineSkillCommands ? "true" : "false") +
					",\"resolvedSkillInvocationToolTarget\":" +
					(preparedChatRequest.resolvedSkillInvocationToolTarget.has_value()
						? blazeclaw::gateway::JsonString(
							preparedChatRequest.resolvedSkillInvocationToolTarget.value())
						: std::string("null")) +
					",\"hasRewrite\":" +
					std::string(preparedChatRequest.rewrittenSkillPromptMessage.has_value()
						? "true"
						: "false") +
					",\"detectedLanguage\":" +
					blazeclaw::gateway::JsonString(detectedLanguage) +
					",\"normalizedIntent\":" +
					blazeclaw::gateway::JsonString(normalizedIntent) +
					",\"fallbackReason\":" +
					(routingFallbackReason.has_value()
						? blazeclaw::gateway::JsonString(routingFallbackReason.value())
						: std::string("null")) +
					"}");

				const auto resolvedPrompt = orchestrator.ResolveSkillsPromptForRun(manager);
				const std::string runtimeMessage =
					ChatRuntimeOrchestrationCoordinator::BuildSkillsInjectedMessage(
						preparedChatRequest.inboundMessageForAgent,
						resolvedPrompt.wide,
						static_cast<std::size_t>(
							manager.m_activeConfig.skills.limits.maxSkillsPromptChars));
				constexpr UINT kMsgAppendToolStatusLine = WM_USER + 0x101;
				const std::wstring runtimeMessageWide = Utf8ToWide(runtimeMessage);
				CMgrMessage::Instance().PostOwnedToolStatusLine(
					kMsgAppendToolStatusLine,
					new CString(runtimeMessageWide.c_str()));
				const std::string activeProvider =
					requestProviderOverride.empty()
					? manager.m_activeChatProvider
					: requestProviderOverride;
				const std::string activeModel =
					requestModelOverride.empty()
					? manager.m_activeChatModel
					: requestModelOverride;
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
