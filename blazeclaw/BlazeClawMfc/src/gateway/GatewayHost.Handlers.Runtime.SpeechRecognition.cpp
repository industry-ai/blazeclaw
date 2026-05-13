#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayRequestParams.h"
#include "GatewayJsonBuilder.h"
#include "Telemetry.h"

#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace handlers::runtime {

		void SpeechRecognitionHandlers::RegisterAll(GatewayHost& host) {
			auto NormalizeMarkdownToPlainText = [](std::string text) {
				auto replaceAll = [](std::string& value, const std::string& from, const std::string& to) {
					std::size_t cursor = 0;
					while ((cursor = value.find(from, cursor)) != std::string::npos) {
						value.replace(cursor, from.size(), to);
						cursor += to.size();
					}
				};

				replaceAll(text, "\r", "");
				replaceAll(text, "```", "");
				replaceAll(text, "`", "");
				replaceAll(text, "**", "");
				replaceAll(text, "__", "");
				replaceAll(text, "*", "");
				replaceAll(text, "#", "");

				std::string normalized;
				normalized.reserve(text.size());
				for (std::size_t index = 0; index < text.size(); ++index) {
					const char current = text[index];
					if ((current == '-' || current == '+') &&
						(index == 0 || text[index - 1] == '\n') &&
						(index + 1 < text.size() && text[index + 1] == ' ')) {
						continue;
					}
					if (current == '[') {
						const std::size_t closeBracket = text.find(']', index + 1);
						const std::size_t openParen =
							closeBracket == std::string::npos
							? std::string::npos
							: text.find('(', closeBracket + 1);
						const std::size_t closeParen =
							openParen == std::string::npos
							? std::string::npos
							: text.find(')', openParen + 1);
						if (closeBracket != std::string::npos &&
							openParen == closeBracket + 1 &&
							closeParen != std::string::npos) {
							normalized.append(text.substr(index + 1, closeBracket - index - 1));
							index = closeParen;
							continue;
						}
					}
					normalized.push_back(current);
				}

				std::string compact;
				compact.reserve(normalized.size());
				bool previousWhitespace = false;
				for (const char ch : normalized) {
					const bool isWhitespace =
						ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
					if (isWhitespace) {
						if (!previousWhitespace) {
							compact.push_back(' ');
						}
						previousWhitespace = true;
						continue;
					}
					compact.push_back(ch);
					previousWhitespace = false;
				}

				if (!compact.empty() && compact.front() == ' ') {
					compact.erase(compact.begin());
				}
				if (!compact.empty() && compact.back() == ' ') {
					compact.pop_back();
				}
				return compact;
			};

			host.RuntimeContext().dispatcher->Register(
				"speech.capabilities.get",
				[&host](const protocol::RequestFrame& request) {
					const bool runtimeConnected = host.IsRunning();
					const bool sttSupported = true;
					const bool sttReady = runtimeConnected;
					const bool incrementalSegmentSupported = true;
					const auto ttsStatus = host.GetSpeechStatus();
					const bool ttsSupported = ttsStatus.supported;
					const bool ttsReady = ttsStatus.ready;

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "stt", JsonObject({
								{ "supported", JsonBool(sttSupported) },
								{ "ready", JsonBool(sttReady) },
								{ "mode", JsonString("record-then-transcribe") },
							}) },
							{ "transcript", JsonObject({
								{ "supportsSegments", JsonBool(incrementalSegmentSupported) },
								{ "supportsInterim", JsonBool(incrementalSegmentSupported) },
								{ "supportsFinal", JsonBool(true) },
							}) },
							{ "tts", JsonObject({
								{ "supported", JsonBool(ttsSupported) },
								{ "ready", JsonBool(ttsReady) },
								{ "speaking", JsonBool(ttsStatus.speaking) },
								{ "provider", JsonString(ttsStatus.provider) },
								{ "model", JsonString(ttsStatus.model) },
								{ "voice", JsonString(ttsStatus.voice) },
								{ "status", JsonString(ttsStatus.status) },
							}) },
							{ "lifecycle", JsonArray({
								JsonString("idle"),
								JsonString("recording"),
								JsonString("paused"),
								JsonString("stopped"),
								JsonString("transcribing"),
								JsonString("completed"),
								JsonString("failed"),
							}) },
						}));
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.speech.startRecording",
				[&host](const protocol::RequestFrame& request) {
					// Start native recording and return an object { ok: bool }
					const auto result = host.StartNativeRecording();
					if (!result.ok) {
						return protocol::ErrorResponse(request, std::string("start_recording_failed"), result.errorMessage);
					}
					const std::string payload = JsonObject({ { "ok", JsonBool(true) } });
					return protocol::OkResponse(request, payload);
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.speech.stopRecording",
				[&host](const protocol::RequestFrame& request) {
					// Stop native recording and return audioPath
					const auto result = host.StopNativeRecording();
					if (!result.ok) {
						return protocol::ErrorResponse(request, std::string("stop_recording_failed"), result.errorMessage);
					}
					const std::string payload = JsonObject({ { "ok", JsonBool(true) }, { "audioPath", JsonString(result.audioPath) } });
					return protocol::OkResponse(request, payload);
				});

			host.RuntimeContext().dispatcher->Register(
				"speech.errorPolicy.get",
				[](const protocol::RequestFrame& request) {
					return protocol::OkResponse(
						request,
						JsonObject({
							{ "defaultClass", JsonString("status") },
							{ "classes", JsonArray({
								JsonString("ignore"),
								JsonString("status"),
								JsonString("toast"),
								JsonString("blocking"),
							}) },
							{ "retry", JsonObject({
								{ "runtime_unavailable", JsonObject({
									{ "class", JsonString("blocking") },
									{ "retryable", JsonBool(true) },
									{ "strategy", JsonString("backoff") },
									{ "guidance", JsonString("Speech runtime unavailable. Ensure runtime/services are running, then retry.") },
								}) },
								{ "cancelled", JsonObject({
									{ "class", JsonString("status") },
									{ "retryable", JsonBool(true) },
									{ "strategy", JsonString("immediate") },
									{ "guidance", JsonString("Transcription was cancelled. Retry if needed.") },
								}) },
							}) },
							{ "map", JsonObject({
								{ "none", JsonString("status") },
								{ "", JsonString("status") },
								{ "cancelled", JsonString("status") },
								{ "aborted", JsonString("ignore") },
								{ "no_speech", JsonString("ignore") },
								{ "runtime_unavailable", JsonString("blocking") },
								{ "speech_recognition_disabled", JsonString("blocking") },
								{ "provider_not_supported", JsonString("blocking") },
								{ "model_not_found", JsonString("blocking") },
								{ "model_load_failed", JsonString("blocking") },
								{ "invalid_input", JsonString("toast") },
								{ "audio_not_found", JsonString("toast") },
								{ "invalid_audio_format", JsonString("toast") },
								{ "audio_decode_failed", JsonString("toast") },
								{ "feature_extraction_failed", JsonString("toast") },
								{ "tokenizer_load_failed", JsonString("blocking") },
								{ "decoder_failed", JsonString("toast") },
								{ "inference_failed", JsonString("toast") },
							}) },
						}));
				});

			host.RuntimeContext().dispatcher->Register(
				"speech.status",
				[&host](const protocol::RequestFrame& request) {
					const auto status = host.GetSpeechStatus();
					EmitTelemetryEvent(
						"gateway.speech.tts.status",
						JsonObject({
							{ "supported", JsonBool(status.supported) },
							{ "ready", JsonBool(status.ready) },
							{ "speaking", JsonBool(status.speaking) },
							{ "status", JsonString(status.status) },
							{ "provider", JsonString(status.provider) },
							{ "model", JsonString(status.model) },
							{ "voice", JsonString(status.voice) },
							{ "utteranceId", JsonString(status.utteranceId) },
							{ "errorCode", JsonString(status.errorCode) },
						}));

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "supported", JsonBool(status.supported) },
							{ "ready", JsonBool(status.ready) },
							{ "speaking", JsonBool(status.speaking) },
							{ "status", JsonString(status.status) },
							{ "provider", JsonString(status.provider) },
							{ "model", JsonString(status.model) },
							{ "voice", JsonString(status.voice) },
							{ "utteranceId", JsonString(status.utteranceId) },
							{ "errorCode", JsonString(status.errorCode) },
							{ "errorMessage", JsonString(status.errorMessage) },
						}));
				});

			host.RuntimeContext().dispatcher->Register(
				"speech.speak",
				[&host, NormalizeMarkdownToPlainText](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string textRaw = params.GetString("text");
					const std::string normalizedText = NormalizeMarkdownToPlainText(textRaw);
					if (normalizedText.empty()) {
						return protocol::ErrorResponse(
							request,
							"invalid_request",
							"speech.speak requires non-empty text");
					}

					const std::string runId = params.GetString("runId");
					const std::string sessionId = params.GetString("sessionId");
					const std::string voice = params.GetString("voice");
					const std::string provider = params.GetString("provider");
					const std::string model = params.GetString("model");

					const auto result = host.SpeakSpeech(
						GatewayHost::SpeechSpeakRequest{
							.runId = runId,
							.sessionId = sessionId,
							.text = normalizedText,
							.voice = voice,
							.provider = provider,
							.model = model,
						});

					EmitTelemetryEvent(
						"gateway.speech.tts.speak",
						JsonObject({
							{ "runId", JsonString(runId) },
							{ "sessionId", JsonString(sessionId) },
							{ "ok", JsonBool(result.ok) },
							{ "cancelled", JsonBool(result.cancelled) },
							{ "speaking", JsonBool(result.speaking) },
							{ "provider", JsonString(result.provider) },
							{ "model", JsonString(result.model) },
							{ "voice", JsonString(result.voice) },
							{ "utteranceId", JsonString(result.utteranceId) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(result.latencyMs)) },
							{ "status", JsonString(result.status) },
							{ "errorCode", JsonString(result.errorCode) },
						}));

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "ok", JsonBool(result.ok) },
							{ "cancelled", JsonBool(result.cancelled) },
							{ "speaking", JsonBool(result.speaking) },
							{ "utteranceId", JsonString(result.utteranceId) },
							{ "text", JsonString(result.normalizedText) },
							{ "audioPath", JsonString(result.audioPath) },
							{ "provider", JsonString(result.provider) },
							{ "model", JsonString(result.model) },
							{ "voice", JsonString(result.voice) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(result.latencyMs)) },
							{ "status", JsonString(result.status) },
							{ "errorCode", JsonString(result.errorCode) },
							{ "errorMessage", JsonString(result.errorMessage) },
						}));
				});

			host.RuntimeContext().dispatcher->Register(
				"speech.stop",
				[&host](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string runId = params.GetString("runId");
					const std::string sessionId = params.GetString("sessionId");
					const std::string utteranceId = params.GetString("utteranceId");

					const auto result = host.StopSpeech(
						GatewayHost::SpeechStopRequest{
							.runId = runId,
							.sessionId = sessionId,
							.utteranceId = utteranceId,
						});

					EmitTelemetryEvent(
						"gateway.speech.tts.stop",
						JsonObject({
							{ "runId", JsonString(runId) },
							{ "sessionId", JsonString(sessionId) },
							{ "ok", JsonBool(result.ok) },
							{ "stopped", JsonBool(result.stopped) },
							{ "utteranceId", JsonString(result.utteranceId) },
							{ "status", JsonString(result.status) },
							{ "errorCode", JsonString(result.errorCode) },
						}));

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "ok", JsonBool(result.ok) },
							{ "stopped", JsonBool(result.stopped) },
							{ "utteranceId", JsonString(result.utteranceId) },
							{ "status", JsonString(result.status) },
							{ "errorCode", JsonString(result.errorCode) },
							{ "errorMessage", JsonString(result.errorMessage) },
						}));
				});

			host.RuntimeContext().dispatcher->Register(
				"speech.transcribe",
				[&host](const protocol::RequestFrame& request) {
					auto stageToString = [](blazeclaw::core::speechrecognition::SpeechSessionStage stage) {
						switch (stage) {
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Idle: return std::string("idle");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Recording: return std::string("recording");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Paused: return std::string("paused");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Stopped: return std::string("stopped");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Transcribing: return std::string("transcribing");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Completed: return std::string("completed");
						case blazeclaw::core::speechrecognition::SpeechSessionStage::Failed: return std::string("failed");
						default: return std::string("unknown");
						}
					};

					const RequestParamsView params(request.paramsJson);
					const std::string audioPath = params.GetString("audioPath");
					const std::string language = params.GetString("language");
					const std::string prompt = params.GetString("prompt");
					const std::string sessionId = params.GetString("sessionId");
					const std::string runId = params.GetString("runId");

					const auto transcribe = host.TranscribeSpeech(
						GatewayHost::SpeechTranscribeRequest{
							.runId = runId,
							.sessionId = sessionId,
							.audioPath = audioPath,
							.language = language,
							.prompt = prompt,
						});

					const std::string normalizedStage = stageToString(transcribe.sessionState.stage);
					const std::string normalizedLanguage =
						transcribe.language.empty()
						? (transcribe.sessionState.language.empty() ? std::string("und") : transcribe.sessionState.language)
						: transcribe.language;
					const std::string normalizedText =
						transcribe.sessionState.transcriptText.empty()
						? transcribe.text
						: transcribe.sessionState.transcriptText;
					const std::uint32_t normalizedLatency =
						transcribe.sessionState.latencyMs == 0
						? transcribe.latencyMs
						: transcribe.sessionState.latencyMs;
					const std::string effectiveSessionId =
						!transcribe.sessionState.sessionId.empty()
						? transcribe.sessionState.sessionId
						: sessionId;
					const std::string effectiveRunId =
						!transcribe.sessionState.runId.empty()
						? transcribe.sessionState.runId
						: runId;
					const bool hasSegment = transcribe.sessionState.segment.has_value();
					auto normalizeSpeechErrorCode = [](const std::string& raw) {
						std::string normalized;
						normalized.reserve(raw.size());
						for (const char ch : raw) {
							if (ch >= 'A' && ch <= 'Z') {
								normalized.push_back(static_cast<char>(ch - 'A' + 'a'));
								continue;
							}
							if (ch == ' ') {
								normalized.push_back('_');
								continue;
							}
							normalized.push_back(ch);
						}
						return normalized;
					};
					const std::string normalizedErrorCode = normalizeSpeechErrorCode(transcribe.errorCode);
					auto resolveErrorClass = [&](const std::string& errorCode, const bool cancelled) {
						if (cancelled || errorCode == "cancelled") {
							return std::string("status");
						}
						if (errorCode == "aborted" || errorCode == "no_speech") {
							return std::string("ignore");
						}
						if (errorCode == "runtime_unavailable" ||
							errorCode == "speech_recognition_disabled" ||
							errorCode == "provider_not_supported" ||
							errorCode == "model_not_found" ||
							errorCode == "model_load_failed" ||
							errorCode == "tokenizer_load_failed") {
							return std::string("blocking");
						}
						if (!errorCode.empty()) {
							return std::string("toast");
						}
						return std::string("status");
					};
					const std::string errorClass = resolveErrorClass(normalizedErrorCode, transcribe.cancelled);

					EmitTelemetryEvent(
						"gateway.speech.lifecycle",
						JsonObject({
							{ "runId", JsonString(effectiveRunId) },
							{ "sessionId", JsonString(effectiveSessionId) },
							{ "stage", JsonString(normalizedStage) },
							{ "ok", JsonBool(transcribe.ok) },
							{ "cancelled", JsonBool(transcribe.cancelled) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(normalizedLatency)) },
							{ "hasSegment", JsonBool(hasSegment) },
							{ "errorCode", JsonString(transcribe.errorCode) },
							{ "errorClass", JsonString(errorClass) },
						}));

					if (hasSegment) {
						EmitTelemetryEvent(
							"gateway.speech.segment",
							JsonObject({
								{ "runId", JsonString(effectiveRunId) },
								{ "sessionId", JsonString(effectiveSessionId) },
								{ "stage", JsonString(normalizedStage) },
								{ "final", JsonBool(transcribe.sessionState.segment->final) },
								{ "sequence", JsonNumber(static_cast<std::uint64_t>(transcribe.sessionState.segment->sequence)) },
							}));
					}

					protocol::ResponseFrame chatResponse{};
					if (transcribe.ok && !transcribe.text.empty()) {
						nlohmann::json forwardedParams = nlohmann::json::object();
						forwardedParams["message"] = transcribe.text;
						forwardedParams["bodyForCommands"] = transcribe.text;
						forwardedParams["bodyForAgent"] = transcribe.text;
						forwardedParams["speechSession"] = nlohmann::json::object({
							{ "sessionId", transcribe.sessionState.sessionId },
							{ "runId", transcribe.sessionState.runId },
							{ "stage", normalizedStage },
							{ "audioPath", transcribe.sessionState.audioPath },
							{ "text", normalizedText },
							{ "language", normalizedLanguage },
							{ "latencyMs", normalizedLatency },
							{ "cancelled", transcribe.sessionState.cancelled },
						});
						if (hasSegment) {
							forwardedParams["speechSession"]["segment"] = nlohmann::json::object({
								{ "text", transcribe.sessionState.segment->text },
								{ "final", transcribe.sessionState.segment->final },
								{ "sequence", transcribe.sessionState.segment->sequence },
							});
						}
						forwardedParams["transcriptInjection"] = nlohmann::json::object({
							{ "source", "voice" },
							{ "ingestMethod", "speech.transcribe" },
							{ "sessionId", effectiveSessionId },
							{ "runId", effectiveRunId },
							{ "requestCorrelationId", request.id },
							{ "orchestrationSurface", "chat.send" },
						});
						forwardedParams["speechArtifact"] = nlohmann::json::object({
							{ "type", "voice_transcript" },
							{ "source", "speech.transcribe" },
							{ "audioPath", transcribe.sessionState.audioPath },
							{ "text", normalizedText },
							{ "language", normalizedLanguage },
							{ "latencyMs", normalizedLatency },
							{ "stage", normalizedStage },
							{ "hasSegment", hasSegment },
						});
						if (hasSegment) {
							forwardedParams["speechArtifact"]["segment"] = nlohmann::json::object({
								{ "text", transcribe.sessionState.segment->text },
								{ "final", transcribe.sessionState.segment->final },
								{ "sequence", transcribe.sessionState.segment->sequence },
							});
						}
						if (!sessionId.empty()) {
							forwardedParams["sessionId"] = sessionId;
						}
						if (!runId.empty()) {
							forwardedParams["runId"] = runId;
						}

						chatResponse = host.RuntimeContext().dispatcher->Dispatch(
							protocol::RequestFrame{
								.id = request.id,
								.method = "chat.send",
								.paramsJson = forwardedParams.dump(),
							});

						EmitTelemetryEvent(
							"gateway.speech.forwarding",
							JsonObject({
								{ "runId", JsonString(effectiveRunId) },
								{ "sessionId", JsonString(effectiveSessionId) },
								{ "targetMethod", JsonString("chat.send") },
								{ "forwardedOk", JsonBool(chatResponse.ok) },
								{ "hasSegment", JsonBool(hasSegment) },
							}));
					}

					const std::string speechSessionJson =
						transcribe.sessionState.segment.has_value()
						? JsonObject({
							{ "sessionId", JsonString(transcribe.sessionState.sessionId) },
							{ "runId", JsonString(transcribe.sessionState.runId) },
							{ "stage", JsonString(normalizedStage) },
							{ "audioPath", JsonString(transcribe.sessionState.audioPath) },
							{ "text", JsonString(normalizedText) },
							{ "language", JsonString(normalizedLanguage) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(normalizedLatency)) },
							{ "cancelled", JsonBool(transcribe.sessionState.cancelled) },
							{ "segment", JsonObject({
								{ "text", JsonString(transcribe.sessionState.segment->text) },
								{ "final", JsonBool(transcribe.sessionState.segment->final) },
								{ "sequence", JsonNumber(static_cast<std::uint64_t>(transcribe.sessionState.segment->sequence)) },
							}) },
						})
						: JsonObject({
							{ "sessionId", JsonString(transcribe.sessionState.sessionId) },
							{ "runId", JsonString(transcribe.sessionState.runId) },
							{ "stage", JsonString(normalizedStage) },
							{ "audioPath", JsonString(transcribe.sessionState.audioPath) },
							{ "text", JsonString(normalizedText) },
							{ "language", JsonString(normalizedLanguage) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(normalizedLatency)) },
							{ "cancelled", JsonBool(transcribe.sessionState.cancelled) },
						});

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "ok", JsonBool(transcribe.ok) },
							{ "cancelled", JsonBool(transcribe.cancelled) },
							{ "text", JsonString(transcribe.text) },
							{ "language", JsonString(normalizedLanguage) },
							{ "sessionId", JsonString(sessionId) },
							{ "runId", JsonString(runId) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(transcribe.latencyMs)) },
							{ "speechSession", speechSessionJson },
							{ "transcriptInjection", JsonObject({
								{ "source", JsonString("voice") },
								{ "ingestMethod", JsonString("speech.transcribe") },
								{ "sessionId", JsonString(effectiveSessionId) },
								{ "runId", JsonString(effectiveRunId) },
								{ "requestCorrelationId", JsonString(request.id) },
								{ "orchestrationSurface", JsonString("chat.send") },
							}) },
							{ "errorPolicy", JsonObject({
								{ "class", JsonString(errorClass) },
								{ "normalizedCode", JsonString(normalizedErrorCode) },
							}) },
							{ "speechArtifact", JsonObject({
								{ "type", JsonString("voice_transcript") },
								{ "source", JsonString("speech.transcribe") },
								{ "audioPath", JsonString(transcribe.sessionState.audioPath) },
								{ "text", JsonString(normalizedText) },
								{ "language", JsonString(normalizedLanguage) },
								{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(normalizedLatency)) },
								{ "stage", JsonString(normalizedStage) },
								{ "hasSegment", JsonBool(hasSegment) },
							}) },
							{ "errorCode", JsonString(transcribe.errorCode) },
							{ "errorMessage", JsonString(transcribe.errorMessage) },
							{ "errorClass", JsonString(errorClass) },
							{ "retry", JsonObject({
								{ "retryable", JsonBool(normalizedErrorCode == "runtime_unavailable" || transcribe.cancelled) },
								{ "strategy", JsonString(normalizedErrorCode == "runtime_unavailable" ? "backoff" : "immediate") },
								{ "guidance", JsonString(normalizedErrorCode == "runtime_unavailable"
									? "Speech runtime unavailable. Verify runtime/services are ready and retry."
									: (transcribe.cancelled
										? "Transcription cancelled. Retry if needed."
										: "Retry after checking microphone/audio input and runtime readiness.")) },
							}) },
							{ "forwardedOk", JsonBool(chatResponse.ok) },
							{ "forwardedMethod", JsonString(transcribe.ok && !transcribe.text.empty() ? "chat.send" : "") },
							{ "forwardedPayload", JsonString(chatResponse.payloadJson.value_or(std::string("{}"))) },
						}));
				});
		}

	} // namespace handlers::runtime

} // namespace blazeclaw::gateway
