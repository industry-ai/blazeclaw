#include "pch.h"
#include "ChatProviderRuntimeService.h"

#include "EmbeddingsService.h"
#include "AgentsModelRoutingService.h"
#include "OnnxEmbeddingsService.h"
#include "PiEmbeddedService.h"
#include "RetrievalMemoryService.h"
#include "runtime/LocalModel/ITextGenerationRuntime.h"

#include <windows.h>

#include <cctype>
#include <string>

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

		std::string NormalizeDeepSeekApiModelId(const std::string& modelId) {
			if (modelId.empty() || modelId == "deepseek") {
				return "deepseek-chat";
			}

			if (modelId == "deepseek/deepseek-chat") {
				return "deepseek-chat";
			}

			if (modelId == "deepseek/deepseek-reasoner") {
				return "deepseek-reasoner";
			}

			return modelId;
		}

		std::string BuildAttachmentSummary(
			const std::vector<std::string>& attachmentMimeTypes) {
			std::string summary = "[attachments]";
			if (attachmentMimeTypes.empty()) {
				summary += "\n- image (mimeType=unknown)";
				return summary;
			}

			for (const auto& mimeType : attachmentMimeTypes) {
				summary += "\n- image (mimeType=";
				summary += mimeType.empty() ? "unknown" : mimeType;
				summary += ")";
			}

			return summary;
		}

		std::string BuildQwen3ChatPrompt(
			const std::string& userMessage,
			const bool hasAttachments,
			const std::vector<std::string>& attachmentMimeTypes,
			const bool strictNoEcho) {
			std::string normalizedUserMessage = userMessage;
			if (normalizedUserMessage.empty()) {
				normalizedUserMessage = "User sent image attachments.";
			}

			if (hasAttachments) {
				normalizedUserMessage += "\n\n";
				normalizedUserMessage += BuildAttachmentSummary(attachmentMimeTypes);
				normalizedUserMessage +=
					"\nInstruction: respond as a text assistant. "
					"Do not repeat the user message.";
			}

			std::string prompt;
			prompt.reserve(normalizedUserMessage.size() + 256);
			prompt += "<|im_start|>system\n";
			prompt += "You are a helpful assistant. Answer the user directly. ";
			prompt += "Do not echo the user prompt verbatim. ";
			if (strictNoEcho) {
				prompt += "Do not quote or repeat the user's wording. ";
				prompt += "Give only the helpful answer content. ";
			}
			prompt += "\n";
			prompt += "<|redacted_im_end|>\n";
			prompt += "<|im_start|>user\n";
			prompt += normalizedUserMessage;
			prompt += "\n<|redacted_im_end|>\n";
			prompt += "<|im_start|>assistant\n";
			return prompt;
		}

		std::string BuildLocalModelPrompt(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
			return BuildQwen3ChatPrompt(
				request.message,
				request.hasAttachments,
				request.attachmentMimeTypes,
				false);
		}

		std::string BuildLocalModelRetryPrompt(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
			return BuildQwen3ChatPrompt(
				request.message,
				request.hasAttachments,
				request.attachmentMimeTypes,
				true);
		}

		std::string TrimAsciiWhitespace(const std::string& value) {
			const std::size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}

			const std::size_t last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		std::string NormalizeForEchoCheck(const std::string& value) {
			const std::string trimmed = TrimAsciiWhitespace(value);
			std::string normalized;
			normalized.reserve(trimmed.size());

			bool previousWasSpace = false;
			for (const char ch : trimmed) {
				const unsigned char code = static_cast<unsigned char>(ch);
				if (std::isspace(code) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				if (std::ispunct(code) != 0) {
					continue;
				}

				normalized.push_back(static_cast<char>(std::tolower(code)));
				previousWasSpace = false;
			}

			return TrimAsciiWhitespace(normalized);
		}

		bool IsLikelyEchoResponse(
			const std::string& userMessage,
			const std::string& assistantText) {
			if (userMessage.empty() || assistantText.empty()) {
				return false;
			}

			const std::string normalizedUser = NormalizeForEchoCheck(userMessage);
			const std::string normalizedAssistant = NormalizeForEchoCheck(assistantText);
			if (normalizedUser.empty() || normalizedAssistant.empty()) {
				return false;
			}

			if (normalizedAssistant == normalizedUser) {
				return true;
			}

			if (normalizedAssistant.size() > normalizedUser.size() &&
				normalizedAssistant.rfind(normalizedUser, 0) == 0) {
				const std::string trailing = TrimAsciiWhitespace(
					normalizedAssistant.substr(normalizedUser.size()));
				return trailing.empty();
			}

			return false;
		}

	} // namespace

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult
		ChatProviderRuntimeService::ExecuteProviderPath(
			const ChatProviderRuntimeBindings& bindings,
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& sessionId,
			const std::string& runtimeMessage,
			const std::string& activeProvider,
			const std::string& activeModel) const {
		if (bindings.config == nullptr ||
			bindings.embeddingsService == nullptr ||
			bindings.retrievalMemoryService == nullptr ||
			bindings.retrievalMemorySnapshot == nullptr ||
			bindings.modelRouting == nullptr ||
			bindings.piEmbedded == nullptr) {
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = activeModel,
				.errorCode = "chat_provider_bindings_incomplete",
				.errorMessage = "internal: chat provider bindings incomplete",
			};
		}

		auto providerRequest = request;
		providerRequest.message = runtimeMessage;

		if (activeProvider == "deepseek") {
			bindings.clearDeepSeekRunCancelled(providerRequest.runId);
			if (!bindings.hasDeepSeekCredential()) {
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = activeModel,
					.errorCode = "deepseek_api_key_missing",
					.errorMessage =
						"DeepSeek API key missing. Configure DeepSeek extension first.",
				};
			}

			const std::string effectiveModel = NormalizeDeepSeekApiModelId(
				activeModel.empty() ? "deepseek-chat" : activeModel);
			const auto apiKey = bindings.resolveDeepSeekCredentialUtf8();
			if (!apiKey.has_value() || apiKey->empty()) {
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = effectiveModel,
					.errorCode = "deepseek_api_key_missing",
					.errorMessage =
						"DeepSeek API key missing. Configure DeepSeek extension first.",
				};
			}

			return bindings.invokeDeepSeekRemoteChat(
				providerRequest,
				effectiveModel,
				apiKey.value());
		}

		if (bindings.localModelActivationEnabled && bindings.localModelRuntime != nullptr &&
			bindings.localModelRuntimeSnapshot != nullptr) {
			const std::string prompt = BuildLocalModelPrompt(providerRequest);
			std::string streamedLocalText;
			std::vector<std::string> streamedLocalSnapshots;
			TRACE(
				"[LocalModel] request.enqueue runId=%s session=%s promptChars=%zu attachments=%s\n",
				providerRequest.runId.c_str(),
				sessionId.c_str(),
				prompt.size(),
				providerRequest.hasAttachments ? "true" : "false");
			TRACE(
				"[LocalModel] request.start runId=%s\n",
				providerRequest.runId.c_str());

			const auto localResult = bindings.localModelRuntime->GenerateStream(
				localmodel::TextGenerationRequest{
					.runId = providerRequest.runId,
					.prompt = prompt,
					.maxTokens = std::nullopt,
					.temperature = std::nullopt,
				},
				[&](const std::string& delta) {
					if (delta.empty()) {
						return;
					}

					streamedLocalText += delta;
					streamedLocalSnapshots.push_back(streamedLocalText);
					if (request.onAssistantDelta) {
						request.onAssistantDelta(streamedLocalText);
					}
				});
			*bindings.localModelRuntimeSnapshot = bindings.localModelRuntime->Snapshot();

			if (!localResult.ok) {
				const std::string errorCode =
					localResult.error.has_value()
					? localmodel::TextGenerationErrorCodeToString(
						localResult.error->code)
					: "chat_runtime_error";
				const std::string errorMessage =
					localResult.error.has_value() &&
					!localResult.error->message.empty()
					? localResult.error->message
					: "local model generation failed";
				TRACE(
					"[LocalModel] request.terminal runId=%s state=%s latencyMs=%u tokens=%u reason=%s\n",
					providerRequest.runId.c_str(),
					localResult.cancelled ? "aborted" : "error",
					localResult.latencyMs,
					localResult.generatedTokens,
					errorMessage.c_str());
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = localResult.modelId,
					.errorCode = errorCode,
					.errorMessage = errorMessage,
				};
			}

			std::string assistantText = streamedLocalText.empty()
				? localResult.text
				: streamedLocalText;
			std::string modelId = localResult.modelId;
			std::uint32_t latencyMs = localResult.latencyMs;
			std::uint32_t generatedTokens = localResult.generatedTokens;
			if (streamedLocalSnapshots.empty() &&
				IsLikelyEchoResponse(request.message, assistantText)) {
				TRACE(
					"[LocalModel] request.retry runId=%s reason=echo_detected\n",
					providerRequest.runId.c_str());
				const std::string retryPrompt = BuildLocalModelRetryPrompt(providerRequest);
				const auto retryResult = bindings.localModelRuntime->GenerateStream(
					localmodel::TextGenerationRequest{
						.runId = providerRequest.runId + "-retry",
						.prompt = retryPrompt,
						.maxTokens = std::nullopt,
						.temperature = std::nullopt,
					},
					nullptr);
				*bindings.localModelRuntimeSnapshot = bindings.localModelRuntime->Snapshot();

				if (retryResult.ok &&
					!IsLikelyEchoResponse(request.message, retryResult.text)) {
					assistantText = retryResult.text;
					modelId = retryResult.modelId;
					latencyMs = retryResult.latencyMs;
					generatedTokens = retryResult.generatedTokens;
				}
			}

			if (IsLikelyEchoResponse(request.message, assistantText)) {
				TRACE(
					"[LocalModel] request.terminal runId=%s state=error latencyMs=%u tokens=%u reason=echo_output_detected\n",
					providerRequest.runId.c_str(),
					latencyMs,
					generatedTokens);
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = modelId,
					.errorCode = "local_model_echo_output",
					.errorMessage = "local model echoed user input",
				};
			}

			TRACE(
				"[LocalModel] request.terminal runId=%s state=final latencyMs=%u tokens=%u\n",
				providerRequest.runId.c_str(),
				latencyMs,
				generatedTokens);

			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = true,
				.assistantText = assistantText,
				.assistantDeltas = streamedLocalSnapshots,
				.modelId = modelId,
				.errorCode = {},
				.errorMessage = {},
			};
		}

		if (bindings.config->localModel.enabled &&
			!bindings.localModelActivationEnabled &&
			bindings.localModelActivationReason != nullptr) {
			TRACE(
				"[LocalModel] request.fallback runId=%s reason=%s rolloutEligible=%s status=%s\n",
				request.runId.c_str(),
				bindings.localModelActivationReason->c_str(),
				bindings.localModelRolloutEligible ? "true" : "false",
				bindings.localModelRuntimeSnapshot != nullptr
					? bindings.localModelRuntimeSnapshot->status.c_str()
					: "");
		}

		const auto modelSelection = bindings.modelRouting->SelectModel(
			bindings.getAgentModelUtf8(),
			"chat.send");

		std::string retrievalContext;
		if (bindings.config->embeddings.enabled && !request.message.empty()) {
			const auto userEmbedding = bindings.embeddingsService->EmbedText(
				EmbeddingRequest{
					.text = Utf8ToWide(request.message),
					.normalize = true,
					.traceId = "chat-retrieval-query",
				});
			if (userEmbedding.ok) {
				const auto matches = bindings.retrievalMemoryService->Query(
					sessionId,
					userEmbedding.vector,
					2);
				if (!matches.empty()) {
					retrievalContext = " [ctx:";
					for (std::size_t i = 0; i < matches.size(); ++i) {
						if (i > 0) {
							retrievalContext += " | ";
						}

						retrievalContext += matches[i].text;
					}

					retrievalContext += "]";
				}

				bindings.retrievalMemoryService->Upsert(
					sessionId,
					"user",
					request.message,
					userEmbedding.vector,
					bindings.currentEpochMs());
				*bindings.retrievalMemorySnapshot = bindings.retrievalMemoryService->Snapshot();
			}
		}

		const auto embeddedRun = bindings.piEmbedded->QueueRun(
			EmbeddedRunRequest{
				.sessionId = sessionId,
				.agentId = "default",
				.message = request.message,
			});

		if (!embeddedRun.accepted) {
			bindings.modelRouting->RecordFailover(
				modelSelection.selectedModel,
				embeddedRun.reason,
				embeddedRun.startedAtMs == 0
				? 1735689800000
				: embeddedRun.startedAtMs);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = activeModel,
				.errorCode = "embedded_run_rejected",
				.errorMessage = embeddedRun.reason,
			};
		}

		const std::string assistantText = request.message.empty()
			? "Received image attachment."
			: ("Model(" + modelSelection.selectedModel + "): " +
				request.message + retrievalContext);

		if (bindings.config->embeddings.enabled && !assistantText.empty()) {
			const auto assistantEmbedding = bindings.embeddingsService->EmbedText(
				EmbeddingRequest{
					.text = Utf8ToWide(assistantText),
					.normalize = true,
					.traceId = "chat-retrieval-index",
				});
			if (assistantEmbedding.ok) {
				bindings.retrievalMemoryService->Upsert(
					sessionId,
					"assistant",
					assistantText,
					assistantEmbedding.vector,
					bindings.currentEpochMs());
				*bindings.retrievalMemorySnapshot = bindings.retrievalMemoryService->Snapshot();
			}
		}

		const bool completed = bindings.piEmbedded->CompleteRun(
			embeddedRun.runId,
			"completed",
			embeddedRun.startedAtMs + 1);
		if (!completed) {
			bindings.modelRouting->RecordFailover(
				modelSelection.selectedModel,
				"embedded_completion_failed",
				embeddedRun.startedAtMs + 1);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelSelection.selectedModel,
				.errorCode = "embedded_completion_failed",
				.errorMessage = "embedded completion failed",
			};
		}

		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = true,
			.assistantText = assistantText,
			.modelId = modelSelection.selectedModel,
			.errorCode = {},
			.errorMessage = {},
		};
	}

} // namespace blazeclaw::core
