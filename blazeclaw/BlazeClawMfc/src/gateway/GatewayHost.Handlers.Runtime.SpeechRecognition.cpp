#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayRequestParams.h"
#include "GatewayJsonBuilder.h"

#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace handlers::runtime {

		void SpeechRecognitionHandlers::RegisterAll(GatewayHost& host) {
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
						if (transcribe.sessionState.segment.has_value()) {
							forwardedParams["speechSession"]["segment"] = nlohmann::json::object({
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
							{ "errorCode", JsonString(transcribe.errorCode) },
							{ "errorMessage", JsonString(transcribe.errorMessage) },
							{ "forwardedOk", JsonBool(chatResponse.ok) },
							{ "forwardedMethod", JsonString(transcribe.ok && !transcribe.text.empty() ? "chat.send" : "") },
							{ "forwardedPayload", JsonString(chatResponse.payloadJson.value_or(std::string("{}"))) },
						}));
				});
		}

	} // namespace handlers::runtime

} // namespace blazeclaw::gateway
