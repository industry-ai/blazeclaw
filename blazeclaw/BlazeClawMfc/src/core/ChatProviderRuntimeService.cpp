#include "pch.h"
#include "ChatProviderRuntimeService.h"

#include "EmbeddingsService.h"
#include "AgentsModelRoutingService.h"
#include "OnnxEmbeddingsService.h"
#include "PiEmbeddedService.h"
#include "RetrievalMemoryService.h"
#include "runtime/LocalModel/ITextGenerationRuntime.h"

#include <windows.h>

#include <array>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

		std::string WideToUtf8(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			const int needed = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (needed <= 0) {
				return {};
			}

			std::string output(static_cast<std::size_t>(needed), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				needed,
				nullptr,
				nullptr);
			return output;
		}

		std::string ToLowerAscii(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::size_t FindCaseInsensitive(
			const std::string& haystack,
			const std::string& needle,
			const std::size_t startPos = 0) {
			if (needle.empty()) {
				return std::string::npos;
			}

			if (startPos >= haystack.size()) {
				return std::string::npos;
			}

			const std::string loweredHaystack = ToLowerAscii(haystack);
			const std::string loweredNeedle = ToLowerAscii(needle);
			return loweredHaystack.find(loweredNeedle, startPos);
		}

		bool IsAllowlistedPromptMarker(const std::string& markerPayload) {
			const std::size_t first = markerPayload.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return false;
			}
			const std::size_t last = markerPayload.find_last_not_of(" \t\r\n");
			const std::string marker = ToLowerAscii(
				markerPayload.substr(first, last - first + 1));
			return marker == "im_start" ||
				marker == "im_end" ||
				marker == "redacted_im_end" ||
				marker == "assistant" ||
				marker == "user" ||
				marker == "system";
		}

		std::vector<std::string> BuildScrubPhrases(
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy) {
			std::vector<std::string> phrases = {
				"[assistant_response]",
				"assistant_response",
				"[user_message]",
				"\nuser\n",
				"\nassistant\n",
				"\rim_start",
				"<|im_start|>",
			};

			for (const auto& extraPhraseWide : sanitizePolicy.extraScrubPhrases) {
				const std::string extraPhrase = WideToUtf8(extraPhraseWide);
				if (!extraPhrase.empty()) {
					phrases.push_back(extraPhrase);
				}
			}

			return phrases;
		}

		std::vector<std::string> BuildTerminalCutMarkers(
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy) {
			std::vector<std::string> markers = {
				"\n[user_message]",
				"\nuser\n",
				"\nassistant\n",
				"<|im_start|>",
			};

			for (const auto& extraMarkerWide : sanitizePolicy.extraTerminalCutMarkers) {
				const std::string extraMarker = WideToUtf8(extraMarkerWide);
				if (!extraMarker.empty()) {
					markers.push_back(extraMarker);
				}
			}

			return markers;
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

		std::string NormalizeForEchoCheck(
			const std::string& value,
			const bool utf8EchoNormalization) {
			const std::string trimmed = TrimAsciiWhitespace(value);
			std::string normalized;
			normalized.reserve(trimmed.size());

			bool previousWasSpace = false;
			for (const unsigned char code : trimmed) {
				if (std::isspace(code) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				if (code < 0x80 && std::ispunct(code) != 0) {
					continue;
				}

				if (code < 0x80) {
					normalized.push_back(static_cast<char>(std::tolower(code)));
				}
				else if (utf8EchoNormalization) {
					normalized.push_back(static_cast<char>(code));
				}
				else {
					normalized.push_back(static_cast<char>(code));
				}
				previousWasSpace = false;
			}

			return TrimAsciiWhitespace(normalized);
		}

		std::vector<std::string> TokenizeNormalizedText(const std::string& normalized) {
			std::vector<std::string> tokens;
			std::istringstream input(normalized);
			std::string token;
			while (input >> token) {
				tokens.push_back(token);
			}
			return tokens;
		}

		double ComputeTokenOverlapRatio(
			const std::vector<std::string>& a,
			const std::vector<std::string>& b) {
			if (a.empty() || b.empty()) {
				return 0.0;
			}

			std::unordered_map<std::string, std::uint32_t> frequencies;
			for (const auto& token : a) {
				++frequencies[token];
			}

			std::uint32_t matches = 0;
			for (const auto& token : b) {
				auto it = frequencies.find(token);
				if (it == frequencies.end() || it->second == 0) {
					continue;
				}

				--it->second;
				++matches;
			}

			const auto denominator = static_cast<double>((std::max)(a.size(), b.size()));
			if (!(denominator > 0.0)) {
				return 0.0;
			}

			return static_cast<double>(matches) / denominator;
		}

		double ComputeTextSimilarity(
			const std::string& left,
			const std::string& right) {
			if (left.empty() || right.empty()) {
				return 0.0;
			}

			const std::size_t maxChars = 512;
			const std::string a = left.substr(0, maxChars);
			const std::string b = right.substr(0, maxChars);

			std::vector<std::size_t> previous(b.size() + 1);
			std::vector<std::size_t> current(b.size() + 1);
			for (std::size_t j = 0; j <= b.size(); ++j) {
				previous[j] = j;
			}

			for (std::size_t i = 1; i <= a.size(); ++i) {
				current[0] = i;
				for (std::size_t j = 1; j <= b.size(); ++j) {
					const std::size_t substitutionCost = a[i - 1] == b[j - 1] ? 0 : 1;
					current[j] = (std::min)({
						previous[j] + 1,
						current[j - 1] + 1,
						previous[j - 1] + substitutionCost,
					});
				}
				previous.swap(current);
			}

			const std::size_t distance = previous[b.size()];
			const double maxLen = static_cast<double>((std::max)(a.size(), b.size()));
			if (!(maxLen > 0.0)) {
				return 0.0;
			}

			return 1.0 - (static_cast<double>(distance) / maxLen);
		}

		bool ShouldStopOnRoleTokenLine(
			const std::string& trimmedLine,
			const std::string& normalizedLine,
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy) {
			if (!sanitizePolicy.roleTokenStopRequiresContext) {
				return normalizedLine == "assistant" || normalizedLine == "user";
			}

			const std::string loweredTrimmed = ToLowerAscii(TrimAsciiWhitespace(trimmedLine));
			return loweredTrimmed == "assistant" || loweredTrimmed == "user";
		}

		bool IsLikelyEchoResponse(
			const std::string& userMessage,
			const std::string& assistantText,
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy) {
			if (userMessage.empty() || assistantText.empty()) {
				return false;
			}

			const std::string normalizedUser = NormalizeForEchoCheck(
				userMessage,
				sanitizePolicy.utf8EchoNormalization);
			const std::string normalizedAssistant = NormalizeForEchoCheck(
				assistantText,
				sanitizePolicy.utf8EchoNormalization);
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

			const auto userTokens = TokenizeNormalizedText(normalizedUser);
			const auto assistantTokens = TokenizeNormalizedText(normalizedAssistant);
			const double overlapRatio = ComputeTokenOverlapRatio(userTokens, assistantTokens);
			const double similarity = ComputeTextSimilarity(normalizedUser, normalizedAssistant);
			if (overlapRatio >= sanitizePolicy.echoTokenOverlapThreshold &&
				similarity >= sanitizePolicy.echoSimilarityThreshold) {
				return true;
			}

			return false;
		}

		bool HasPromptLeakage(
			const std::string& userMessage,
			const std::string& assistantText,
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy) {
			if (assistantText.empty()) {
				return false;
			}

			if (assistantText.find("[user_message]") != std::string::npos ||
				assistantText.find("assistant_response") != std::string::npos) {
				return true;
			}

			const std::string normalizedUser = NormalizeForEchoCheck(
				userMessage,
				sanitizePolicy.utf8EchoNormalization);
			const std::string normalizedAssistant = NormalizeForEchoCheck(
				assistantText,
				sanitizePolicy.utf8EchoNormalization);
			if (normalizedUser.empty() || normalizedAssistant.empty()) {
				return false;
			}

			return normalizedAssistant.find(normalizedUser) != std::string::npos;
		}

		std::string SanitizeLocalAssistantText(
			const std::string& userMessage,
			const blazeclaw::config::LocalModelConfig::SanitizationConfig& sanitizePolicy,
			std::string value) {
			if (!sanitizePolicy.enabled) {
				return TrimAsciiWhitespace(value);
			}

			for (;;) {
				const auto markerStart = value.find("<|");
				if (markerStart == std::string::npos) {
					break;
				}
				const auto markerEnd = value.find("|>", markerStart + 2);
				if (markerEnd == std::string::npos) {
					if (!sanitizePolicy.stripOnlyAllowlistedMarkers) {
						value.erase(markerStart);
					}
					break;
				}

				const std::string markerPayload =
					value.substr(markerStart + 2, markerEnd - markerStart - 2);
				const bool shouldStrip = !sanitizePolicy.stripOnlyAllowlistedMarkers ||
					IsAllowlistedPromptMarker(markerPayload);
				if (!shouldStrip) {
					break;
				}

				value.erase(markerStart, markerEnd - markerStart + 2);
			}

			const auto scrubPhrases = BuildScrubPhrases(sanitizePolicy);
			for (const auto& phrase : scrubPhrases) {
				std::size_t offset = 0;
				for (;;) {
					offset = sanitizePolicy.scrubCaseInsensitive
						? FindCaseInsensitive(value, phrase, offset)
						: value.find(phrase, offset);
					if (offset == std::string::npos) {
						break;
					}
					value.erase(offset, phrase.size());
				}
			}

			const auto terminalCutMarkers = BuildTerminalCutMarkers(sanitizePolicy);
			std::size_t cutPos = std::string::npos;
			for (const auto& marker : terminalCutMarkers) {
				const auto pos = sanitizePolicy.scrubCaseInsensitive
					? FindCaseInsensitive(value, marker)
					: value.find(marker);
				if (pos != std::string::npos) {
					cutPos = cutPos == std::string::npos ? pos : (std::min)(cutPos, pos);
				}
			}
			if (cutPos != std::string::npos) {
				value.erase(cutPos);
			}

			const std::string normalizedUser = NormalizeForEchoCheck(
				userMessage,
				sanitizePolicy.utf8EchoNormalization);
			std::vector<std::string> keptLines;
			std::unordered_map<std::string, std::uint32_t> normalizedLineCounts;
			std::istringstream input(value);
			std::string line;
			while (std::getline(input, line)) {
				const std::string trimmedLine = TrimAsciiWhitespace(line);
				if (trimmedLine.empty()) {
					continue;
				}

				const std::string normalizedLine = NormalizeForEchoCheck(
					trimmedLine,
					sanitizePolicy.utf8EchoNormalization);
				if (ShouldStopOnRoleTokenLine(trimmedLine, normalizedLine, sanitizePolicy)) {
					break;
				}

				if (!normalizedUser.empty()) {
					if (!normalizedLine.empty() && normalizedLine == normalizedUser) {
						continue;
					}

					if (normalizedLine.find(normalizedUser) != std::string::npos) {
						continue;
					}

					if (normalizedUser.find(normalizedLine) != std::string::npos &&
						normalizedLine.size() >= sanitizePolicy.minSubstringEchoChars) {
						continue;
					}
				}

				if (!normalizedLine.empty()) {
					auto& count = normalizedLineCounts[normalizedLine];
					if (count >= sanitizePolicy.repeatedLineAllowance + 1) {
						break;
					}
					++count;
				}

				keptLines.push_back(trimmedLine);
			}

			if (!keptLines.empty()) {
				std::string rebuilt;
				for (std::size_t i = 0; i < keptLines.size(); ++i) {
					if (i > 0) {
						rebuilt += "\n";
					}
					rebuilt += keptLines[i];
				}
				value = std::move(rebuilt);
			}

			return TrimAsciiWhitespace(value);
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
			const auto& sanitizePolicy = bindings.config->localModel.sanitize;
			const std::string prompt = BuildLocalModelPrompt(providerRequest);
			double runtimeTemperature = bindings.config->localModel.temperature;
			if (!(runtimeTemperature > 0.0)) {
				runtimeTemperature = 0.7;
			}
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
					.temperature = runtimeTemperature,
				},
				[&](const std::string& delta) {
					if (delta.empty()) {
						return;
					}

					// GenerateStream passes cumulative assistant text, not a per-token delta slice.
					streamedLocalText = delta;
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

			// Prefer the runtime's final trimmed text. Stream callbacks must match, but using the
			// canonical `localResult.text` avoids gateway streamCursor vs assistantText size mismatch
			// (which prevents terminal chat events from enqueueing and leaves the UI blank).
			std::string assistantText = !localResult.text.empty()
				? localResult.text
				: streamedLocalText;
			assistantText = SanitizeLocalAssistantText(
				request.message,
				sanitizePolicy,
				std::move(assistantText));
			std::string modelId = localResult.modelId;
			std::uint32_t latencyMs = localResult.latencyMs;
			std::uint32_t generatedTokens = localResult.generatedTokens;
			const bool finalTextReplacementSignaled =
				sanitizePolicy.emitFinalTextReplacementSignal &&
				!streamedLocalSnapshots.empty() &&
				streamedLocalSnapshots.back() != assistantText;
			const bool emptyAfterSanitize =
				sanitizePolicy.enforceNonEmptyAfterSanitize && assistantText.empty();
			if (HasPromptLeakage(request.message, assistantText, sanitizePolicy) ||
				IsLikelyEchoResponse(request.message, assistantText, sanitizePolicy) ||
				emptyAfterSanitize) {
				TRACE(
					"[LocalModel] request.retry runId=%s reason=%s\n",
					providerRequest.runId.c_str(),
					emptyAfterSanitize
					? "empty_after_sanitize"
					: "prompt_leak_or_echo_detected");
				const std::string retryPrompt = BuildLocalModelRetryPrompt(providerRequest);
				const auto retryResult = bindings.localModelRuntime->GenerateStream(
					localmodel::TextGenerationRequest{
						.runId = providerRequest.runId + "-retry",
						.prompt = retryPrompt,
						.maxTokens = std::nullopt,
						.temperature = runtimeTemperature,
					},
					nullptr);
				*bindings.localModelRuntimeSnapshot = bindings.localModelRuntime->Snapshot();

				if (retryResult.ok) {
					const std::string sanitizedRetryText =
						SanitizeLocalAssistantText(
							request.message,
							sanitizePolicy,
							retryResult.text);
					const bool retryEmptyAfterSanitize =
						sanitizePolicy.enforceNonEmptyAfterSanitize &&
						sanitizedRetryText.empty();
					if (!retryEmptyAfterSanitize &&
						!HasPromptLeakage(request.message, sanitizedRetryText, sanitizePolicy) &&
						!IsLikelyEchoResponse(request.message, sanitizedRetryText, sanitizePolicy)) {
						assistantText = sanitizedRetryText;
						modelId = retryResult.modelId;
						latencyMs = retryResult.latencyMs;
						generatedTokens = retryResult.generatedTokens;
					}
					else if (retryEmptyAfterSanitize) {
						assistantText.clear();
					}
				}
			}

			if (sanitizePolicy.enforceNonEmptyAfterSanitize && assistantText.empty()) {
				TRACE(
					"[LocalModel] request.terminal runId=%s state=error latencyMs=%u tokens=%u reason=empty_after_sanitize\n",
					providerRequest.runId.c_str(),
					latencyMs,
					generatedTokens);
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = modelId,
					.errorCode = "local_model_empty_after_sanitize",
					.errorMessage = "local model produced empty output after sanitization",
				};
			}

			if (IsLikelyEchoResponse(request.message, assistantText, sanitizePolicy)) {
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
				.finalTextReplaced = finalTextReplacementSignaled,
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
