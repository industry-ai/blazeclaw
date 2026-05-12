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

					protocol::ResponseFrame chatResponse{};
					if (transcribe.ok && !transcribe.text.empty()) {
						nlohmann::json forwardedParams = nlohmann::json::object();
						forwardedParams["message"] = transcribe.text;
						forwardedParams["bodyForCommands"] = transcribe.text;
						forwardedParams["bodyForAgent"] = transcribe.text;
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

					return protocol::OkResponse(
						request,
						JsonObject({
							{ "ok", JsonBool(transcribe.ok) },
							{ "cancelled", JsonBool(transcribe.cancelled) },
							{ "text", JsonString(transcribe.text) },
							{ "language", JsonString(transcribe.language.empty() ? "und" : transcribe.language) },
							{ "sessionId", JsonString(sessionId) },
							{ "runId", JsonString(runId) },
							{ "latencyMs", JsonNumber(static_cast<std::uint64_t>(transcribe.latencyMs)) },
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
