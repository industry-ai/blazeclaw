#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayHostChatPipelineRouteDeps.h"
#include "ChatPipelineRequestNormalization.h"
#include "GatewayHostRuntimeLocalHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayJsonUtils.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "ChatRunStageContext.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "RuntimeSequencingPolicy.h"
#include "RuntimeToolCallNormalizer.h"
#include "RuntimeTranscriptGuard.h"
#include "RecoveryPolicyEngine.h"
#include "SendPolicyResolver.h"
#include "ToolPolicyPipeline.h"
#include "TranscriptPolicyResolver.h"
#include "GatewayLifecycleEventEmitter.h"
#include "ChatMessageLimits.h"
#include "RunSummaryBuilder.h"
#include "BranchDecisionDiagnostics.h"
#include "ChatTranscriptStore.h"
#include "ChatAbortCoordinator.h"
#include "ChatHistoryPolicy.h"
#include "ChatRoutePolicy.h"
#include "ChatOrchestrationPolicy.h"
#include "ToolEventRecipientPolicy.h"
#include "ChatControlPlaneService.h"
#include "GatewayEventFanoutService.h"
#include "executors/EmailScheduleExecutor.h"
#include "../app/BlazeClawMfcApp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <regex>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace handlers::runtime {

		void ChatPipelineHandlers::RegisterAll(GatewayHost& host) {
			using namespace blazeclaw::gateway::runtime_local;
			const bool fastSyntheticRevealMode = []() {
				char* raw = nullptr;
				std::size_t len = 0;
				if (_dupenv_s(&raw, &len, "BLAZECLAW_CHAT_POLL_SYNTHETIC_REVEAL_FAST_MODE") != 0 ||
					raw == nullptr ||
					len == 0) {
					if (raw != nullptr) {
						free(raw);
					}
					return true;
				}
				std::string lowered(raw);
				free(raw);
				std::transform(
					lowered.begin(),
					lowered.end(),
					lowered.begin(),
					[](const unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});
				return lowered == "1" ||
					lowered == "true" ||
					lowered == "yes" ||
					lowered == "on";
				}();
			const std::uint64_t syntheticRevealMaxDurationMs = 5000;

			const ChatPipelineRouteDeps routeDeps = ChatPipelineRouteDeps::Bind(host);
			GatewayMethodDispatcher* const dispatcher = routeDeps.dispatch.dispatcher;
			GatewaySessionRegistry* const sessionRegistry = routeDeps.session.sessionRegistry;
			const auto& runtime = routeDeps.runtime;
			const auto& run = routeDeps.runTracking;
			const auto& sessions = routeDeps.sessionQueues;
			const auto& callbacks = routeDeps.callbacks;
			const auto& taskDeltas = routeDeps.taskDeltas;
			const auto& pollMetrics = routeDeps.pollMetrics;
			const auto& model = routeDeps.modelRouting;
			const auto& skills = routeDeps.skills;
			const auto& configSchema = routeDeps.configSchema;

			dispatcher->Register(
				"agent",
				[dispatcher](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					return dispatcher->Dispatch(forwarded);
				});
			dispatcher->Register(
				"send",
				[dispatcher](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					return dispatcher->Dispatch(forwarded);
				});
			struct SessionCompactionBranchRecord {
				std::string branchId;
				std::string sessionId;
				std::string baseBranchId;
				std::string title;
				std::uint64_t createdAtMs = 0;
				std::uint64_t restoredAtMs = 0;
			};
			struct SessionOperatorState {
				std::unordered_map<std::string, std::unordered_set<std::string>> sessionSubscribers;
				std::unordered_map<std::string, std::unordered_set<std::string>> sessionMessageSubscribers;
				std::unordered_map<std::string, SessionCompactionBranchRecord> compactionBranches;
				std::unordered_map<std::string, std::vector<std::string>> compactionBranchIdsBySession;
				std::uint64_t compactionSequence = 1;
			};
			auto sessionState = std::make_shared<SessionOperatorState>();
			ChatPipelineRequestNormalization requestNormalization(*sessionRegistry);

			dispatcher->Register(
				"sessions.subscribe",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const auto route = requestNormalization.NormalizeSubscriberRoute(request.paramsJson);
					const std::string& sessionId = route.sessionId;
					const std::string& connectionId = route.connectionId;
					auto& subscribers = sessionState->sessionSubscribers[sessionId];
					subscribers.insert(connectionId);
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"connectionId", JsonString(connectionId)},
							{"subscribed", JsonBool(true)},
							{"subscriberCount", JsonNumber(static_cast<std::uint64_t>(subscribers.size()))},
							}));
				});
			dispatcher->Register(
				"sessions.unsubscribe",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const auto route = requestNormalization.NormalizeSubscriberRoute(request.paramsJson);
					const std::string& sessionId = route.sessionId;
					const std::string& connectionId = route.connectionId;
					auto it = sessionState->sessionSubscribers.find(sessionId);
					if (it != sessionState->sessionSubscribers.end()) {
						it->second.erase(connectionId);
					}
					const std::uint64_t remaining = it != sessionState->sessionSubscribers.end()
						? static_cast<std::uint64_t>(it->second.size())
						: 0;
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"connectionId", JsonString(connectionId)},
							{"subscribed", JsonBool(false)},
							{"subscriberCount", JsonNumber(remaining)},
							}));
				});

			dispatcher->Register(
				"sessions.messages.subscribe",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const auto route = requestNormalization.NormalizeSubscriberRoute(request.paramsJson);
					const std::string& sessionId = route.sessionId;
					const std::string& connectionId = route.connectionId;
					auto& subscribers = sessionState->sessionMessageSubscribers[sessionId];
					subscribers.insert(connectionId);
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"connectionId", JsonString(connectionId)},
							{"subscribed", JsonBool(true)},
							{"subscriberCount", JsonNumber(static_cast<std::uint64_t>(subscribers.size()))},
							}));
				});
			dispatcher->Register(
				"sessions.messages.unsubscribe",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const auto route = requestNormalization.NormalizeSubscriberRoute(request.paramsJson);
					const std::string& sessionId = route.sessionId;
					const std::string& connectionId = route.connectionId;
					auto it = sessionState->sessionMessageSubscribers.find(sessionId);
					if (it != sessionState->sessionMessageSubscribers.end()) {
						it->second.erase(connectionId);
					}
					const std::uint64_t remaining = it != sessionState->sessionMessageSubscribers.end()
						? static_cast<std::uint64_t>(it->second.size())
						: 0;
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"connectionId", JsonString(connectionId)},
							{"subscribed", JsonBool(false)},
							{"subscriberCount", JsonNumber(remaining)},
							}));
				});

			dispatcher->Register(
				"sessions.send",
				[dispatcher, requestNormalization](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					const protocol::ResponseFrame response = dispatcher->Dispatch(forwarded);
					if (!response.ok) {
						return response;
					}
					const std::string sessionId = requestNormalization.ResolveSessionId(request.paramsJson);
					EmitTelemetryEvent(
						"gateway.event.session.message",
						JsonObject({
							{"event", JsonString("session.message")},
							{"sessionId", JsonString(sessionId)},
							{"ts", JsonNumber(GatewayEpochMilliseconds())},
							}));
					EmitTelemetryEvent(
						"gateway.event.session.tool",
						JsonObject({
							{"event", JsonString("session.tool")},
							{"sessionId", JsonString(sessionId)},
							{"ts", JsonNumber(GatewayEpochMilliseconds())},
							}));
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"forwardedMethod", JsonString("chat.send")},
							{"response", response.payloadJson.value_or(std::string("{}"))},
							}));
				});
			dispatcher->Register(
				"sessions.steer",
				[dispatcher, requestNormalization](const protocol::RequestFrame& request) {
					auto abortForwarded = request;
					abortForwarded.method = "chat.abort";
					const protocol::ResponseFrame abortResponse = dispatcher->Dispatch(abortForwarded);
					if (!abortResponse.ok) {
						return abortResponse;
					}

					auto sendForwarded = request;
					sendForwarded.method = "chat.send";
					const protocol::ResponseFrame sendResponse = dispatcher->Dispatch(sendForwarded);
					if (!sendResponse.ok) {
						return sendResponse;
					}

					const bool interruptedActiveRun =
						abortResponse.payloadJson.has_value() &&
						abortResponse.payloadJson.value().find("\"aborted\":true") != std::string::npos;
					const std::string sessionId =
						requestNormalization.ResolveSessionId(request.paramsJson);
					const std::string sendPayload = sendResponse.payloadJson.value_or(std::string("{}"));
					const std::string trimmed = json::Trim(sendPayload);
					if (!trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}') {
						std::string merged = trimmed;
						merged.pop_back();
						if (merged.size() > 1) {
							merged += ",";
						}
						merged += "\"sessionId\":" + JsonString(sessionId) + ",";
						merged += "\"interruptedActiveRun\":" + std::string(interruptedActiveRun ? "true" : "false") + "}";
						return protocol::OkResponse(request, merged);
					}

					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"interruptedActiveRun", JsonBool(interruptedActiveRun)},
							{"response", sendPayload},
							}));
				});
			dispatcher->Register(
				"sessions.abort",
				[dispatcher](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.abort";
					const protocol::ResponseFrame response = dispatcher->Dispatch(forwarded);
					if (!response.ok) {
						return response;
					}
					return protocol::OkResponse(
						request,
						JsonObject({
							{"aborted", JsonBool(true)},
							{"forwardedMethod", JsonString("chat.abort")},
							{"response", response.payloadJson.value_or(std::string("{}"))},
							}));
				});

			dispatcher->Register(
				"sessions.compaction.list",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const std::string sessionId = requestNormalization.ResolveSessionId(request.paramsJson);
					auto idsIt = sessionState->compactionBranchIdsBySession.find(sessionId);
					std::vector<std::string> rows;
					if (idsIt != sessionState->compactionBranchIdsBySession.end()) {
						for (const auto& branchId : idsIt->second) {
							auto branchIt = sessionState->compactionBranches.find(branchId);
							if (branchIt == sessionState->compactionBranches.end()) {
								continue;
							}
							rows.push_back(JsonObject({
								{"branchId", JsonString(branchIt->second.branchId)},
								{"sessionId", JsonString(branchIt->second.sessionId)},
								{"title", JsonString(branchIt->second.title)},
								{"baseBranchId", JsonString(branchIt->second.baseBranchId)},
								{"createdAtMs", JsonNumber(branchIt->second.createdAtMs)},
								{"restoredAtMs", JsonNumber(branchIt->second.restoredAtMs)},
								}));
						}
					}
					return protocol::OkResponse(
						request,
						JsonObject({
							{"sessionId", JsonString(sessionId)},
							{"items", JsonArray(rows)},
							{"count", JsonNumber(static_cast<std::uint64_t>(rows.size()))},
							}));
				});
			dispatcher->Register(
				"sessions.compaction.get",
				[sessionState](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string branchId = params.GetString("branchId");
					if (branchId.empty()) {
						return protocol::ErrorResponse(request, "invalid_request", "branchId required");
					}
					auto it = sessionState->compactionBranches.find(branchId);
					if (it == sessionState->compactionBranches.end()) {
						return protocol::OkResponse(
							request,
							JsonObject({
								{"branchId", JsonString(branchId)},
								{"found", JsonBool(false)},
								}));
					}
					return protocol::OkResponse(
						request,
						JsonObject({
							{"branchId", JsonString(it->second.branchId)},
							{"sessionId", JsonString(it->second.sessionId)},
							{"title", JsonString(it->second.title)},
							{"baseBranchId", JsonString(it->second.baseBranchId)},
							{"createdAtMs", JsonNumber(it->second.createdAtMs)},
							{"restoredAtMs", JsonNumber(it->second.restoredAtMs)},
							{"found", JsonBool(true)},
							}));
				});
			dispatcher->Register(
				"sessions.compaction.branch",
				[sessionState, requestNormalization](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string sessionId = requestNormalization.ResolveSessionId(request.paramsJson);
					const std::string title = params.GetString("title").empty()
						? "compaction-branch"
						: params.GetString("title");
					const std::string baseBranchId = params.GetString("baseBranchId");
					SessionCompactionBranchRecord record;
					record.sessionId = sessionId;
					record.baseBranchId = baseBranchId;
					record.title = title;
					record.branchId = "compaction-" + std::to_string(sessionState->compactionSequence++);
					record.createdAtMs = GatewayEpochMilliseconds();
					sessionState->compactionBranches.insert_or_assign(record.branchId, record);
					sessionState->compactionBranchIdsBySession[sessionId].push_back(record.branchId);
					return protocol::OkResponse(
						request,
						JsonObject({
							{"created", JsonBool(true)},
							{"branchId", JsonString(record.branchId)},
							{"sessionId", JsonString(record.sessionId)},
							{"baseBranchId", JsonString(record.baseBranchId)},
							{"title", JsonString(record.title)},
							{"createdAtMs", JsonNumber(record.createdAtMs)},
							}));
				});
			dispatcher->Register(
				"sessions.compaction.restore",
				[sessionState](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string branchId = params.GetString("branchId");
					if (branchId.empty()) {
						return protocol::ErrorResponse(request, "invalid_request", "branchId required");
					}
					auto it = sessionState->compactionBranches.find(branchId);
					if (it == sessionState->compactionBranches.end()) {
						return protocol::ErrorResponse(request, "invalid_request", "branchId not found");
					}
					it->second.restoredAtMs = GatewayEpochMilliseconds();
					return protocol::OkResponse(
						request,
						JsonObject({
							{"restored", JsonBool(true)},
							{"branchId", JsonString(it->second.branchId)},
							{"sessionId", JsonString(it->second.sessionId)},
							{"restoredAtMs", JsonNumber(it->second.restoredAtMs)},
							}));
				});

			dispatcher->Register(
				"chat.send",
				[runtime, run, sessions, callbacks, taskDeltas, model, skills](const protocol::RequestFrame& request) {
					ChatRunStageContext stageContext{
						.requestId = request.id,
						.method = request.method,
						.paramsJson = request.paramsJson,
						.validateAttachments = [](
							const std::optional<std::string>& paramsJson,
							bool& hasAttachments,
							std::string& errorCode,
							std::string& errorMessage) {
							return ValidateAttachmentPayloadShape(
								paramsJson,
								hasAttachments,
								errorCode,
								errorMessage);
						},
						.findRunByIdempotency = [&run](const std::string& key)
							-> std::optional<std::string> {
							if (key.empty()) {
								return std::nullopt;
							}

							const auto dedupeIt = run.runByIdempotency.find(key);
							if (dedupeIt == run.runByIdempotency.end()) {
								return std::nullopt;
							}

							return dedupeIt->second;
						},
						.extractAttachmentMimeTypes = [](
							const std::optional<std::string>& paramsJson) {
							return ExtractAttachmentMimeTypes(paramsJson);
						},
					};
					auto pipelineResult = runtime.chatRunPipeline->Run(stageContext);
					EmitTelemetryEvent(
						"gateway.chat.pipeline.stages",
						std::string("{\"requestId\":") +
						JsonString(stageContext.requestId) +
						",\"runId\":" +
						JsonString(stageContext.runId) +
						",\"sessionKey\":" +
						JsonString(stageContext.sessionKey) +
						",\"forceError\":" +
						std::string(stageContext.forceError ? "true" : "false") +
						",\"hasAttachmentPayload\":" +
						std::string(stageContext.hasAttachmentPayload ? "true" : "false") +
						",\"normalizedMessageChars\":" +
						std::to_string(stageContext.normalizedMessage.size()) +
						",\"method\":" +
						JsonString(stageContext.method) +
						",\"status\":" +
						JsonString(pipelineResult.status) +
						",\"stages\":" +
						SerializeStringArrayLocal(stageContext.stageTrace) +
						"}");

					if (stageContext.shouldReturnEarly) {
						if (stageContext.skippedByInlinePolicy) {
							EmitTelemetryEvent(
								"gateway.chat.inline.skip",
								std::string("{\"requestId\":") +
								JsonString(request.id) +
								",\"reason\":" +
								JsonString(stageContext.skippedReasonCode) +
								"}");
							return protocol::OkResponseOptionalPayload(request, stageContext.responsePayloadJson);
						}

						if (stageContext.deduped) {
							const auto replayIt =
								run.replayByIdempotency.find(stageContext.idempotencyKey);
							if (replayIt != run.replayByIdempotency.end()) {
								return protocol::ReplayFromStored(
									request,
									replayIt->second.ok,
									replayIt->second.payloadJson,
									replayIt->second.error);
							}

							return protocol::OkResponse(request, "{\"runId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"queued\":false,\"deduped\":true"
								",\"promptRunId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"responders\":[]}");
						}

						if (stageContext.responseError.has_value()) {
							return protocol::ErrorResponse(request, std::move(*stageContext.responseError));
						}
						return protocol::ErrorResponse(
							request,
							BuildRuntimeErrorShape(
								stageContext.responseErrorCode,
								stageContext.responseErrorMessage,
								stageContext.runId,
								stageContext.sessionKey));
					}

					const std::string requestedSessionKey = stageContext.requestedSessionKey;
					const std::string sessionKey = stageContext.sessionKey;
					const std::string message = stageContext.message;
					const std::string normalizedMessage = stageContext.normalizedMessage;
					const std::string idempotencyKey = stageContext.idempotencyKey;
					const bool detachedSend = stageContext.detached;
					const std::string clientConnectionId = stageContext.clientConnectionId;
					const bool forceError = stageContext.forceError;
					const bool hasAttachments = stageContext.hasAttachmentPayload;
					const RequestParamsView sendParams(request.paramsJson);
					auto ToLowerTrimmed = [](const std::string& raw) {
						std::string normalized = json::Trim(raw);
						std::transform(
							normalized.begin(),
							normalized.end(),
							normalized.begin(),
							[](const unsigned char ch) {
								return static_cast<char>(std::tolower(ch));
							});
						return normalized;
						};
					const std::string responseMode =
						ToLowerTrimmed(sendParams.GetString("responseMode"));
					const bool multiActiveRequested = responseMode == "multi_active";
					std::vector<std::string> requestedResponderTokens;
					std::string requestedRespondersRaw;
					if (json::FindRawField(
						request.paramsJson.value_or(std::string()),
						"requestedResponders",
						requestedRespondersRaw)) {
						try {
							const auto parsed = nlohmann::json::parse(requestedRespondersRaw);
							if (parsed.is_array()) {
								for (const auto& item : parsed) {
									if (!item.is_string()) {
										continue;
									}

									const std::string token = ToLowerTrimmed(item.get<std::string>());
									if (!token.empty()) {
										requestedResponderTokens.push_back(token);
									}
								}
							}
						}
						catch (...) {
						}
					}
					const std::string requestedModelRaw =
						sendParams.GetString("model");
					const std::string requestedModelOverride =
						requestedModelRaw.empty()
						? std::string()
						: GatewayModel::NormalizeModelId(requestedModelRaw);
					std::string requestedProviderOverride =
						sendParams.GetString("providerOverride");
					const std::string requestedProviderOverrideNormalized =
						ToLowerTrimmed(requestedProviderOverride);
					std::string transcriptInjectionRaw;
					const bool hasTranscriptInjection =
						json::FindRawField(request.paramsJson.value_or(std::string()), "transcriptInjection", transcriptInjectionRaw);
					std::string speechArtifactRaw;
					const bool hasSpeechArtifact =
						json::FindRawField(request.paramsJson.value_or(std::string()), "speechArtifact", speechArtifactRaw);
					std::string transcriptSource = "typed";
					std::string transcriptSessionId;
					std::string transcriptRunId;
					if (hasTranscriptInjection) {
						json::FindStringField(transcriptInjectionRaw, "source", transcriptSource);
						json::FindStringField(transcriptInjectionRaw, "sessionId", transcriptSessionId);
						json::FindStringField(transcriptInjectionRaw, "runId", transcriptRunId);
						transcriptSource = json::Trim(transcriptSource);
						transcriptSessionId = json::Trim(transcriptSessionId);
						transcriptRunId = json::Trim(transcriptRunId);
						if (transcriptSource.empty()) {
							transcriptSource = "voice";
						}
					}
					const std::uint64_t nowMs = stageContext.nowEpochMs > 0
						? stageContext.nowEpochMs
						: CurrentEpochMsLocal();
					const std::string runId = !stageContext.runId.empty()
						? stageContext.runId
						: (!request.id.empty()
							? request.id
							: ("chat-run-" + std::to_string(nowMs) +
								"-" + std::to_string(run.runsById.size() + 1)));

					struct ChatSendResponderManifestEntry {
						std::string responderRunId;
						std::string responderId;
						std::string provider;
						std::string model;
						std::string runtimeKind;
						std::string responderLabel;
						std::uint32_t responderOrder = 0;
					};
					std::vector<ChatSendResponderManifestEntry> responderManifest;
					std::unordered_set<std::string> responderIdSet;
					bool localResponderEnabled = true;
					std::uint32_t maxActiveResponders = 1;
					if (const auto* appConfig =
						dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
						appConfig != nullptr) {
						localResponderEnabled = appConfig->Config().localModel.enabled;
						maxActiveResponders =
							std::max<std::uint32_t>(
								1,
								appConfig->Config().multiActive.maxActiveResponders);
					}
					if (!multiActiveRequested) {
						maxActiveResponders = 1;
					}
					const bool remoteDeepSeekReady = !model.runtimeDeepSeekApiKey.empty();

					auto normalizeLocalModelId = [](const std::string& rawModelId) {
						if (rawModelId.empty() ||
							GatewayModel::IsDeepSeekModelId(rawModelId)) {
							return std::string(GatewayModel::kDefaultModelId);
						}

						return GatewayModel::NormalizeModelId(rawModelId);
						};
					auto normalizeDeepSeekModelId = [&](const std::string& rawModelId) {
						const std::string configuredDefault =
							model.runtimeDeepSeekDefaultModel.empty()
							? std::string(GatewayModel::kDeepSeekChatModelId)
							: model.runtimeDeepSeekDefaultModel;
						if (rawModelId.empty()) {
							return configuredDefault;
						}

						if (!GatewayModel::IsDeepSeekModelId(rawModelId)) {
							return configuredDefault;
						}

						return GatewayModel::NormalizeModelId(rawModelId);
						};
					auto appendResponder = [&](const std::string& provider,
						const std::string& model,
						const std::string& runtimeKind) {
						if (provider.empty() || model.empty() || runtimeKind.empty()) {
							return;
						}

						const std::string responderId =
							ToLowerTrimmed(provider) + ":" + ToLowerTrimmed(model);
						if (!responderIdSet.insert(responderId).second) {
							return;
						}

						const std::string labelSuffix = runtimeKind == "local"
							? " (Local)"
							: " (Remote)";
						responderManifest.push_back(
							ChatSendResponderManifestEntry{
								.responderRunId = std::string(),
								.responderId = responderId,
								.provider = provider,
								.model = model,
								.runtimeKind = runtimeKind,
								.responderLabel =
									GatewayModel::ResolveModelDisplayName(model) + labelSuffix,
								.responderOrder = 0,
							});
						};

					auto maybeAppendLocal = [&](const std::string& modelHint) {
						if (!localResponderEnabled) {
							return;
						}

						appendResponder(
							"local",
							normalizeLocalModelId(modelHint),
							"local");
						};
					auto maybeAppendDeepSeek = [&](const std::string& modelHint) {
						if (!remoteDeepSeekReady) {
							return;
						}

						appendResponder(
							"deepseek",
							normalizeDeepSeekModelId(modelHint),
							"remote");
						};

					auto appendByToken = [&](const std::string& rawToken) {
						const std::string token = ToLowerTrimmed(rawToken);
						if (token.empty()) {
							return;
						}

						if (token == "local" ||
							token == "seed") {
							maybeAppendLocal(std::string());
							return;
						}

						if (token == "deepseek" ||
							token == "remote") {
							maybeAppendDeepSeek(std::string());
							return;
						}

						const auto colonPos = token.find(':');
						if (colonPos != std::string::npos && colonPos > 0) {
							const std::string provider = token.substr(0, colonPos);
							const std::string model = token.substr(colonPos + 1);
							if (provider == "local" || provider == "seed") {
								maybeAppendLocal(model);
								return;
							}
							if (provider == "deepseek" || provider == "remote") {
								maybeAppendDeepSeek(model);
								return;
							}
						}

						if (token.rfind("deepseek/", 0) == 0) {
							maybeAppendDeepSeek(token);
							return;
						}

						maybeAppendLocal(token);
						};

					if (!requestedResponderTokens.empty()) {
						for (const auto& token : requestedResponderTokens) {
							appendByToken(token);
						}
					}
					else {
						const bool requestedDeepSeekOnly =
							requestedProviderOverrideNormalized == "deepseek" ||
							GatewayModel::IsDeepSeekModelId(requestedModelOverride);
						const bool requestedLocalOnly =
							requestedProviderOverrideNormalized == "local" ||
							requestedProviderOverrideNormalized == "seed";

						if (requestedDeepSeekOnly) {
							maybeAppendDeepSeek(requestedModelOverride);
						}
						else if (requestedLocalOnly) {
							maybeAppendLocal(requestedModelOverride);
						}
						else {
							maybeAppendLocal(requestedModelOverride);
							if (multiActiveRequested) {
								maybeAppendDeepSeek(std::string());
							}
						}
					}

					if (responderManifest.empty()) {
						appendResponder(
							"local",
							std::string(GatewayModel::kDefaultModelId),
							"local");
					}

					std::vector<ChatSendResponderManifestEntry> orderedResponders;
					orderedResponders.reserve(responderManifest.size());
					for (const auto& responder : responderManifest) {
						if (responder.runtimeKind == "local") {
							orderedResponders.push_back(responder);
						}
					}
					for (const auto& responder : responderManifest) {
						if (responder.runtimeKind != "local") {
							orderedResponders.push_back(responder);
						}
					}
					responderManifest = std::move(orderedResponders);

					if (responderManifest.size() > maxActiveResponders) {
						responderManifest.resize(maxActiveResponders);
					}

					for (std::size_t index = 0; index < responderManifest.size(); ++index) {
						auto& responder = responderManifest[index];
						responder.responderOrder =
							static_cast<std::uint32_t>(index);
						responder.responderRunId = index == 0
							? runId
							: runId + ".responder." + std::to_string(index + 1);
					}

					std::string effectiveRequestedModelOverride = requestedModelOverride;
					std::string effectiveRequestedProviderOverride = requestedProviderOverride;
					if (!responderManifest.empty() &&
						responderManifest.front().runtimeKind == "remote") {
						effectiveRequestedProviderOverride = responderManifest.front().provider;
						effectiveRequestedModelOverride = responderManifest.front().model;
					}
					else if (!responderManifest.empty() &&
						(!requestedResponderTokens.empty() ||
							requestedProviderOverrideNormalized == "local" ||
							requestedProviderOverrideNormalized == "seed")) {
						effectiveRequestedProviderOverride.clear();
						effectiveRequestedModelOverride = responderManifest.front().model;
					}

					std::string responderManifestJson = "[";
					for (std::size_t index = 0; index < responderManifest.size(); ++index) {
						if (index > 0) {
							responderManifestJson += ",";
						}

						const auto& responder = responderManifest[index];
						responderManifestJson += JsonObject({
							{"responderRunId", JsonString(responder.responderRunId)},
							{"responderId", JsonString(responder.responderId)},
							{"provider", JsonString(responder.provider)},
							{"model", JsonString(responder.model)},
							{"runtimeKind", JsonString(responder.runtimeKind)},
							{"responderLabel", JsonString(responder.responderLabel)},
							{"responderOrder", JsonNumber(static_cast<std::uint64_t>(responder.responderOrder))},
							});
					}
					responderManifestJson += "]";
					const ChatTranscriptStore transcriptStore;
					bool userTurnPersisted = false;
					auto persistUserTurnIfNeeded = [&]() {
						if (userTurnPersisted) {
							return;
						}

						if (!detachedSend && (!normalizedMessage.empty() || hasAttachments)) {
							const auto userPersisted = transcriptStore.AppendUserMessage(
								ChatTranscriptStore::AppendParams{
									.sessionKey = sessionKey,
									.role = "user",
									.message = normalizedMessage.empty()
										? std::string("[attachment]")
										: normalizedMessage,
									.label = hasAttachments ? "attachments" : std::string(),
									.idempotencyKey = runId + ":user",
								});
							if (!userPersisted.ok && !userPersisted.error.empty()) {
								EmitTelemetryEvent(
									"gateway.chat.transcript.user.persist.error",
									std::string("{\"runId\":") + JsonString(runId) +
									",\"sessionKey\":" + JsonString(sessionKey) +
									",\"error\":" + JsonString(userPersisted.error) + "}");
							}
						}

						if (!detachedSend) {
							PushHistoryMessageIfNew(
								sessions.historyBySession[sessionKey],
								BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));
						}
						userTurnPersisted = true;
						};

					persistUserTurnIfNeeded();
					const bool runAlreadyTracked =
						run.runsById.find(runId) != run.runsById.end();
					const bool lateJoinRequested =
						runAlreadyTracked &&
						stageContext.hasConnectedClient &&
						!clientConnectionId.empty();
					const bool pushLifecycleEnabled =
						stageContext.pushLifecycleRequested;

					ChatControlPlaneService controlPlaneService;
					const bool hasRegisteredRecipient =
						!clientConnectionId.empty() &&
						runtime.transportRecipientRegistry->HasRecipients(runId);
					const auto sendControlDecision =
						controlPlaneService.EvaluateSendControl(
							ChatControlPlaneService::SendControlInput{
								.sessionKey = sessionKey,
								.deliver = stageContext.deliver,
								.routeChannel = stageContext.routeChannel,
								.routeTo = stageContext.routeTo,
								.clientMode = stageContext.clientMode,
								.hasConnectedClient = stageContext.hasConnectedClient,
								.mainKey = stageContext.mainKey,
								.clientCaps = stageContext.clientCaps,
								.runId = runId,
								.hasRegisteredRecipient = hasRegisteredRecipient,
								.lateJoinRequested = lateJoinRequested,
							});
					if (sendControlDecision.toolEvents.wantsToolEvents &&
						!clientConnectionId.empty()) {
						runtime.transportRecipientRegistry->RegisterRecipient(
							runId,
							sessionKey,
							clientConnectionId,
							nowMs);
						runtime.transportRecipientRegistry->RegisterLateJoin(
							sessionKey,
							clientConnectionId,
							nowMs);
						run.toolEventRecipientsByRun[runId].insert(clientConnectionId);
						for (const auto& [activeRunId, activeRun] : run.runsById) {
							if (activeRunId != runId &&
								activeRun.sessionKey == sessionKey &&
								activeRun.active) {
								run.toolEventRecipientsByRun[activeRunId].insert(clientConnectionId);
							}
						}
						runtime.transportRecipientRegistry->PruneExpired(nowMs);
					}

					if (lateJoinRequested && !clientConnectionId.empty()) {
						auto& replayQueue = sessions.eventsBySession[sessionKey];
						const auto activeRuns =
							runtime.transportRecipientRegistry->ActiveRunsForSession(sessionKey);
						for (const auto& activeRunId : activeRuns) {
							const auto activeRunIt = run.runsById.find(activeRunId);
							if (activeRunIt == run.runsById.end()) {
								continue;
							}

							const auto& activeRun = activeRunIt->second;
							if (!activeRun.active ||
								RuntimeTranscriptGuard::IsSilentReplyText(activeRun.assistantText)) {
								continue;
							}

							std::string replayText;
							if (activeRun.providerDeltaCursor > 0 &&
								activeRun.providerDeltaCursor <= activeRun.providerDeltas.size()) {
								replayText = activeRun.providerDeltas[activeRun.providerDeltaCursor - 1];
							}
							if (replayText.empty()) {
								replayText = activeRun.assistantText.substr(
									0,
									(std::min)(activeRun.assistantText.size(), std::size_t{ 64 }));
							}

							if (replayText.empty()) {
								continue;
							}

							const std::string replayMessage =
								BuildAssistantDeltaMessageJson(replayText);
							PushEventWithRetentionLimit(replayQueue, GatewayHost::ChatEventState{
									.runId = activeRun.runId,
									.sessionKey = activeRun.sessionKey,
									.state = "delta",
									.messageJson = replayMessage,
									.errorMessage = std::nullopt,
									.approvalRequired = false,
									.approvalToken = std::nullopt,
									.approvalTokenExpiresAtEpochMs = std::nullopt,
									.approvalNextAction = std::nullopt,
									.terminalReason = std::nullopt,
									.timestampMs = nowMs,
								});
							BranchDecisionDiagnostics::Emit(
								activeRun.runId,
								"controlplane",
								"late_join_replay",
								"delta_replayed",
								std::string("{\"connectionId\":") +
								JsonString(clientConnectionId) +
								",\"sessionKey\":" +
								JsonString(sessionKey) + "}");
						}
					}
					EmitTelemetryEvent(
						"gateway.chat.controlplane.decision",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"route\":{\"originatingChannel\":" +
						JsonString(sendControlDecision.route.originatingChannel) +
						",\"explicitDeliverRoute\":" +
						std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false") +
						",\"reasonCode\":" +
						JsonString(sendControlDecision.route.reasonCode) +
						"},\"toolEvents\":{\"allowed\":" +
						std::string(sendControlDecision.toolEvents.wantsToolEvents ? "true" : "false") +
						",\"reasonCode\":" +
						JsonString(sendControlDecision.toolEvents.reasonCode) +
						"},\"inputSource\":" + JsonString(hasTranscriptInjection ? transcriptSource : "typed") +
						",\"voiceTranscriptInjected\":" + std::string(hasTranscriptInjection ? "true" : "false") +
						"}"
					);
					EmitTelemetryEvent(
						"gateway.chat.orchestration.surface.parity",
						JsonObject({
							{"runId", JsonString(runId)},
							{"sessionKey", JsonString(sessionKey)},
							{"inputSource", JsonString(hasTranscriptInjection ? transcriptSource : "typed")},
							{"voiceTranscriptInjected", JsonBool(hasTranscriptInjection)},
							{"orchestrationSurface", JsonString("chat.send")},
							{"originatingChannel", JsonString(sendControlDecision.route.originatingChannel)},
							{"explicitDeliverRoute", JsonBool(sendControlDecision.route.explicitDeliverRoute)},
							}));

					const std::vector<std::string> attachmentMimeTypes =
						stageContext.attachmentMimeTypes;
					const SendPolicyDecision sendPolicyDecision =
						SendPolicyResolver::Evaluate(
							sessionKey,
							normalizedMessage,
							hasAttachments,
							attachmentMimeTypes);
					if (!sendPolicyDecision.allowed) {
						BranchDecisionDiagnostics::Emit(
							runId,
							"transport_control",
							"send_policy",
							"denied_send",
							std::string("{\"hits\":") +
							SerializeStringArrayLocal(sendPolicyDecision.policyHits) +
							"}");
						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"send\",\"reason\":\"denied_send\"}");
						return protocol::ErrorResponse(
							request,
							BuildRuntimeErrorShape(
								"denied_send",
								"Request denied by send policy.",
								runId,
								sessionKey));
					}
					auto persistTaskDeltas =
						[&taskDeltas, &runId, &sessionKey](
							const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& runtimeTaskDeltas,
							const bool success) {
								if (runtimeTaskDeltas.empty()) {
									return;
								}

								std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> normalizedTaskDeltas;
								normalizedTaskDeltas.reserve(runtimeTaskDeltas.size());
								for (std::size_t index = 0; index < runtimeTaskDeltas.size(); ++index) {
									normalizedTaskDeltas.push_back(
										TaskDeltaLegacyAdapter::AdaptEntry(
											runtimeTaskDeltas[index],
											runId,
											sessionKey,
											index));
								}

								std::string schemaErrorCode;
								std::string schemaErrorMessage;
								if (!TaskDeltaSchemaValidator::ValidateRun(
									runId,
									normalizedTaskDeltas,
									schemaErrorCode,
									schemaErrorMessage)) {
									return;
								}

								const bool upserted =
									taskDeltas.repository.Upsert(runId, normalizedTaskDeltas);
								(void)upserted;
								const std::size_t evictedFromChat =
									taskDeltas.repository.EnforceRetentionLimit(
										taskDeltas.retentionLimit);
								if (evictedFromChat > 0) {
									EmitTelemetryEvent(
										"gateway.taskdelta.retention.evicted",
										std::string("{\"evictedRuns\":") +
										std::to_string(evictedFromChat) +
										",\"remainingRuns\":" +
										std::to_string(taskDeltas.repository.Size()) +
										",\"reason\":\"chat_runtime_upsert\",\"runId\":" +
										JsonString(runId) + "}");
								}
								taskDeltas.persist();
								for (const auto& delta : normalizedTaskDeltas) {
									EmitTelemetryEvent(
										"gateway.taskdelta.transition",
										std::string("{\"runId\":") +
										JsonString(runId) +
										",\"phase\":" + JsonString(delta.phase) +
										",\"toolName\":" + JsonString(delta.toolName) +
										",\"status\":" + JsonString(delta.status) +
										",\"index\":" + std::to_string(delta.index) +
										",\"latencyMs\":" + std::to_string(delta.latencyMs) +
										"}");

								}

								std::string terminalStatus = success ? "completed" : "failed";
								std::string terminalErrorCode;
								for (auto it = normalizedTaskDeltas.rbegin();
									it != normalizedTaskDeltas.rend();
									++it) {
									if (it->phase != "final") {
										continue;
									}

									if (!it->status.empty()) {
										terminalStatus = it->status;
									}

									terminalErrorCode = it->errorCode;
									break;
								}

								if (terminalStatus == "completed") {
									++taskDeltas.runSuccessCount;
								}
								else {
									++taskDeltas.runFailureCount;
								}

								if (terminalErrorCode == "embedded_deadline_exceeded") {
									++taskDeltas.runTimeoutCount;
								}

								if (terminalErrorCode == "embedded_run_cancelled" ||
									terminalStatus == "skipped") {
									++taskDeltas.runCancelledCount;
								}

								if (terminalErrorCode.find("fallback") != std::string::npos ||
									terminalStatus == "fallback") {
									++taskDeltas.runFallbackCount;
								}

								EmitTelemetryEvent(
									"gateway.taskdelta.runSummary",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"count\":" + std::to_string(normalizedTaskDeltas.size()) +
									",\"success\":" + (success ? std::string("true") : std::string("false")) +
									",\"terminalStatus\":" + JsonString(terminalStatus) +
									",\"errorCode\":" + JsonString(terminalErrorCode) +
									",\"totals\":{\"success\":" + std::to_string(taskDeltas.runSuccessCount) +
									",\"failure\":" + std::to_string(taskDeltas.runFailureCount) +
									",\"timeout\":" + std::to_string(taskDeltas.runTimeoutCount) +
									",\"cancelled\":" + std::to_string(taskDeltas.runCancelledCount) +
									",\"fallback\":" + std::to_string(taskDeltas.runFallbackCount) + "}" +
									"}");
						};

					std::string assistantText;
					if (message.empty() && hasAttachments) {
						assistantText = IsLikelyChinesePromptLocal(normalizedMessage)
							? Utf8LiteralLocal(u8"\u5DF2\u6536\u5230\u56FE\u7247\u9644\u4EF6\u3002")
							: "Received image attachment.";
					}
					std::vector<std::string> assistantDeltas;
					std::string backendErrorCode;
					std::string backendErrorMessage;
					std::string backendErrorContextJson;
					std::string terminalState = "final";
					bool approvalRequired = false;
					std::string approvalToken;
					std::uint64_t approvalTokenExpiresAtEpochMs = 0;
					std::string approvalNextAction;
					std::string terminalReason;
					bool failed = false;
					bool orchestrationHandled = false;
					bool lifecycleEventsEnqueued = false;
					bool providerStreamed = false;
					const auto orchestrationPolicy =
						ChatOrchestrationPolicy::Evaluate(
							ChatOrchestrationPolicy::Input{
								.orchestrationPath = model.embeddedOrchestrationPath,
								.message = normalizedMessage,
								.forceError = forceError,
								.hasAttachments = hasAttachments,
							});
					const std::string orchestrationPath =
						orchestrationPolicy.selectedPath;
					const bool allowPromptOrchestration =
						orchestrationPolicy.compatDeterministicEnabled;
					const bool forceWeatherEmailDeterministicOrchestration =
						orchestrationPolicy.intentDeterministicEnabled;
					const bool allowDeterministicPromptOrchestration =
						orchestrationPolicy.deterministicEnabled;
					model.latestOrchestrationPathSelection.runId = runId;
					model.latestOrchestrationPathSelection.path = orchestrationPath;
					model.latestOrchestrationPathSelection.compatDeterministicEnabled =
						allowPromptOrchestration;
					model.latestOrchestrationPathSelection.intentDeterministicEnabled =
						forceWeatherEmailDeterministicOrchestration;
					model.latestOrchestrationPathSelection.deterministicEnabled =
						allowDeterministicPromptOrchestration;
					model.latestOrchestrationPathSelection.decisionReasonCode =
						orchestrationPolicy.decisionReasonCode;
					model.latestOrchestrationPathSelection.decompositionMetadataSource =
						orchestrationPolicy.decompositionMetadataSource;
					model.latestOrchestrationPathSelection.orderedPolicyMode =
						orchestrationPolicy.orderedPolicyMode;
					model.latestOrchestrationPathSelection.orderedPolicyStrict =
						orchestrationPolicy.orderedPolicyStrict;
					model.latestOrchestrationPathSelection.fallbackPolicyProfile =
						orchestrationPolicy.fallbackPolicyProfile;
					model.latestOrchestrationPathSelection.observedAtEpochMs = nowMs;
					EmitTelemetryEvent(
						"gateway.chat.orchestration.pathSelection",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"path\":" +
						JsonString(orchestrationPath) +
						",\"compatDeterministicEnabled\":" +
						std::string(allowPromptOrchestration ? "true" : "false") +
						",\"intentDeterministicEnabled\":" +
						std::string(
							forceWeatherEmailDeterministicOrchestration ? "true" : "false") +
						",\"deterministicEnabled\":" +
						std::string(
							allowDeterministicPromptOrchestration ? "true" : "false") +
						",\"decisionReasonCode\":" +
						JsonString(orchestrationPolicy.decisionReasonCode) +
						",\"decompositionMetadataSource\":" +
						JsonString(orchestrationPolicy.decompositionMetadataSource) +
						",\"orderedPolicyDecision\":" +
						JsonString(orchestrationPolicy.orderedPolicyDecision) +
						",\"orderedPolicyMode\":" +
						JsonString(orchestrationPolicy.orderedPolicyMode) +
						",\"orderedPolicyStrict\":" +
						std::string(orchestrationPolicy.orderedPolicyStrict
							? "true"
							: "false") +
						",\"orderedPolicyTargets\":" +
						SerializeStringArrayLocal(orchestrationPolicy.orderedPolicyTargets) +
						",\"fallbackPolicyProfile\":" +
						JsonString(orchestrationPolicy.fallbackPolicyProfile) +
						",\"allowlistPolicyHint\":" +
						JsonString(orchestrationPolicy.allowlistPolicyHint) +
						",\"fallbackPolicyHint\":" +
						JsonString(orchestrationPolicy.fallbackPolicyHint) +
						",\"dynamicRuntimeDefault\":true}");
					BranchDecisionDiagnostics::Emit(
						runId,
						"runtime",
						"orchestration.pathSelection",
						orchestrationPolicy.decisionReasonCode);
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"orchestration\",\"reason\":" +
						JsonString(orchestrationPolicy.decisionReasonCode) +
						",\"decompositionSource\":" +
						JsonString(orchestrationPolicy.decompositionMetadataSource) +
						",\"orderingMode\":" +
						JsonString(orchestrationPolicy.orderedPolicyMode) +
						",\"fallbackPolicyProfile\":" +
						JsonString(orchestrationPolicy.fallbackPolicyProfile) + "}");
					const auto runtimeToolsSnapshot = runtime.toolRegistry->List();
					auto isRuntimeToolReady = [&runtimeToolsSnapshot](const std::string& expectedToolId) {
						const std::string expectedNormalized = json::Trim(expectedToolId);
						for (const auto& tool : runtimeToolsSnapshot) {
							if (!tool.enabled) {
								continue;
							}

							if (json::Trim(tool.id) == expectedNormalized) {
								return true;
							}
						}

						return false;
						};
					const bool weatherLookupReady = isRuntimeToolReady("weather.lookup");
					const bool emailScheduleReady = isRuntimeToolReady("email.schedule");
					EmitTelemetryEvent(
						"gateway.chat.runtime.required_tools.readiness",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"weatherLookupReady\":" +
						std::string(weatherLookupReady ? "true" : "false") +
						",\"emailScheduleReady\":" +
						std::string(emailScheduleReady ? "true" : "false") +
						",\"runtimeToolsCount\":" +
						std::to_string(runtimeToolsSnapshot.size()) + "}");
					OrderedSequencePolicyOverride orderedSequencePolicyOverride{};
					const OrderedSequencePolicyOverride* orderedSequencePolicyOverridePtr =
						nullptr;
					if (orchestrationPolicy.orderedPolicyMode != "none" &&
						!orchestrationPolicy.orderedPolicyTargets.empty()) {
						orderedSequencePolicyOverride.orderedTargets =
							orchestrationPolicy.orderedPolicyTargets;
						orderedSequencePolicyOverride.strictAllowlist =
							orchestrationPolicy.orderedPolicyStrict;
						orderedSequencePolicyOverride.source =
							orchestrationPolicy.decompositionMetadataSource;
						orderedSequencePolicyOverridePtr = &orderedSequencePolicyOverride;
					}
					auto orderedSequencePreflight =
						RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
							normalizedMessage,
							runtimeToolsSnapshot,
							skills.catalogState.entries,
							orderedSequencePolicyOverridePtr);
					const bool preferChineseResponse =
						stageContext.preferChineseResponse;
					std::vector<std::string> orderedAllowlistTargets;
					bool enforceOrderedAllowlist = false;
					if (orderedSequencePreflight.enforced &&
						(!orderedSequencePreflight.resolvedToolTargets.empty()) &&
						(orderedSequencePreflight.strictAllowlist
							? orderedSequencePreflight.missingTargets.empty()
							: true)) {
						enforceOrderedAllowlist = true;
						orderedAllowlistTargets.reserve(
							orderedSequencePreflight.resolvedToolTargets.size());
						for (const auto& resolvedToolId :
							orderedSequencePreflight.resolvedToolTargets) {
							if (!RuntimeSequencingPolicy::IsResolvedRuntimeToolTarget(
								resolvedToolId,
								runtimeToolsSnapshot)) {
								enforceOrderedAllowlist = false;
								orderedAllowlistTargets.clear();
								break;
							}

							orderedAllowlistTargets.push_back(resolvedToolId);
						}
					}
					const ToolPolicyDecision toolPolicyDecision =
						ToolPolicyPipeline::Build(
							enforceOrderedAllowlist,
							orderedAllowlistTargets,
							runtimeToolsSnapshot);
					if (!toolPolicyDecision.allowAll &&
						toolPolicyDecision.allowedTargets.empty()) {
						BranchDecisionDiagnostics::Emit(
							runId,
							"runtime",
							"tool_policy",
							"tool_policy_block");
						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"tool\",\"reason\":\"tool_policy_block\"}");
					}
					else {
						orderedAllowlistTargets = toolPolicyDecision.allowedTargets;
						enforceOrderedAllowlist = !toolPolicyDecision.allowAll;
						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"tool\",\"reason\":" +
							JsonString(toolPolicyDecision.reasonCode) + "}");
					}

					const TranscriptPolicyDecision transcriptPolicyDecision =
						TranscriptPolicyResolver::Resolve(
							stageContext.runtimeMessage,
							"deepseek");
					if (transcriptPolicyDecision.applied) {
						BranchDecisionDiagnostics::Emit(
							runId,
							"runtime",
							"transcript_policy",
							"transcript_policy_applied");
						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"transcript\",\"reason\":\"transcript_policy_applied\"}");
					}

					const std::string runtimeMessage =
						(orderedSequencePreflight.enforced && !enforceOrderedAllowlist)
						? (std::string(preferChineseResponse
							? Utf8LiteralLocal(u8"\u6709\u5E8F\u6267\u884C\u6B65\u9AA4\uFF08\u4FDD\u6301\u987A\u5E8F\uFF09\uFF1A")
							: "Ordered execution steps (preserve order): ") +
							RuntimeSequencingPolicy::JoinOrderedResolution(
								orderedSequencePreflight) +
							"\n\n" + transcriptPolicyDecision.sanitizedMessage)
						: transcriptPolicyDecision.sanitizedMessage;
					BranchDecisionDiagnostics::EmitWithPayloadSummary(
						runId,
						"runtime",
						"runtime_message",
						"runtime_message_built",
						runtimeMessage,
						256);
					std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> orderedPreflightTaskDeltas;

					if (forceError) {
						failed = true;
						backendErrorCode = "forced_error";
						backendErrorMessage = "forced error for deterministic verification";
					}

					auto buildAssistantDeltasFromTaskDeltas =
						[&runId](
							const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas) {
								std::vector<std::string> deltas;
								for (const auto& delta : taskDeltas) {
									if (delta.phase == "tool_call" && !delta.toolName.empty()) {
										deltas.push_back("tools.execute.start tool=" + delta.toolName);
										continue;
									}

									if (delta.phase == "tool_result" && !delta.toolName.empty()) {
										if (delta.toolName == "email.schedule") {
											EmitTelemetryEvent(
												"gateway.email.fallback.attempt",
												std::string("{\"runId\":") +
												JsonString(runId) +
												",\"tool\":\"email.schedule\",\"status\":" +
												JsonString(delta.status) +
												",\"backend\":" +
												JsonString(delta.fallbackBackend) +
												",\"action\":" +
												JsonString(delta.fallbackAction) +
												",\"attempt\":" +
												std::to_string(delta.fallbackAttempt) +
												",\"maxAttempts\":" +
												std::to_string(delta.fallbackMaxAttempts) +
												"}");
										}

										std::string toolLine =
											"tools.execute.result tool=" +
											delta.toolName +
											" status=" +
											(delta.status.empty() ? std::string("ok") : delta.status) +
											(delta.errorCode.empty()
												? std::string()
												: (" errorCode=" + delta.errorCode));
										if (!delta.errorMessage.empty()) {
											toolLine +=
												" errorMessage=" +
												blazeclaw::gateway::json::SanitizeInlineToolSummary(
													delta.errorMessage);
										}
										deltas.push_back(std::move(toolLine));
										continue;
									}
								}

								return deltas;
						};
					auto hasTerminalTaskDelta = [](
						const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas) {
							return std::any_of(
								taskDeltas.begin(),
								taskDeltas.end(),
								[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
									return delta.phase == "final";
								});
						};
					auto appendForcedTerminalTaskDeltaIfMissing = [&hasTerminalTaskDelta](
						std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
						const std::string& runIdValue,
						const std::string& sessionKeyValue,
						const std::string& terminalStatus,
						const std::string& terminalErrorCodeValue,
						const std::string& terminalErrorMessageValue) {
							if (hasTerminalTaskDelta(taskDeltas)) {
								return;
							}

							const std::uint64_t now = CurrentEpochMsLocal();
							taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
								.index = taskDeltas.size(),
								.runId = runIdValue,
								.sessionId = sessionKeyValue,
								.phase = "final",
								.resultJson = terminalErrorMessageValue,
								.status = terminalStatus,
								.errorCode = terminalErrorCodeValue,
								.startedAtMs = now,
								.completedAtMs = now,
								.latencyMs = 0,
								.stepLabel = "run_terminal",
								});
						};
					auto mergeWithPreflightTaskDeltas = [
						&orderedPreflightTaskDeltas,
						&runId,
						&sessionKey,
						&appendForcedTerminalTaskDeltaIfMissing](
							std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> runtimeTaskDeltas,
							const bool runFailed,
							const std::string& runErrorCode,
							const std::string& runErrorMessage) {
								if (runFailed) {
									appendForcedTerminalTaskDeltaIfMissing(
										runtimeTaskDeltas,
										runId,
										sessionKey,
										"failed",
										runErrorCode,
										runErrorMessage);
								}
								if (orderedPreflightTaskDeltas.empty()) {
									return runtimeTaskDeltas;
								}

								std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
								mergedTaskDeltas.reserve(
									orderedPreflightTaskDeltas.size() + runtimeTaskDeltas.size());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orderedPreflightTaskDeltas.begin(),
									orderedPreflightTaskDeltas.end());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									runtimeTaskDeltas.begin(),
									runtimeTaskDeltas.end());
								return mergedTaskDeltas;
						};

					if (!forceError &&
						!hasAttachments &&
						orderedSequencePreflight.enforced) {
						const bool fallbackAllowedForPolicyDerivedStrict =
							orderedSequencePreflight.strictAllowlist &&
							orderedSequencePolicyOverridePtr != nullptr &&
							orderedSequencePreflight.explicitCallTargets.empty();
						if (fallbackAllowedForPolicyDerivedStrict &&
							!orderedSequencePreflight.missingTargets.empty()) {
							orderedSequencePreflight.strictAllowlist = false;
							EmitTelemetryEvent(
								"gateway.chat.ordered.preflight.strict_downgraded",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"reason\":\"policy_derived_missing_targets\"" +
								",\"missingTargets\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
								",\"missingRuntimeToolIds\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingResolvedToolTargets) + "}");
						}

						orderedPreflightTaskDeltas =
							RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
								runId,
								sessionKey,
								orderedSequencePreflight,
								false,
								{},
								{});

						EmitTelemetryEvent(
							"gateway.chat.ordered.preflight",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"enforced\":true,\"steps\":" +
							SerializeStringArrayLocal(orderedSequencePreflight.orderedTargets) +
							",\"resolvedTools\":" +
							SerializeStringArrayLocal(orderedSequencePreflight.resolvedToolTargets) +
							",\"strictAllowlist\":" +
							std::string(
								orderedSequencePreflight.strictAllowlist ? "true" : "false") +
							",\"missing\":" +
							SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
							"}");

						if (orderedSequencePreflight.strictAllowlist &&
							!orderedSequencePreflight.missingTargets.empty()) {
							const std::string strictMissingErrorCode =
								"ordered_sequence_target_unavailable";
							const std::string missingTargetsJoined =
								RuntimeSequencingPolicy::JoinOrderedTargets(
									orderedSequencePreflight.missingTargets);
							const std::string missingResolvedJoined =
								RuntimeSequencingPolicy::JoinOrderedTargets(
									orderedSequencePreflight.missingResolvedToolTargets);
							const std::string strictMissingErrorMessage =
								"Ordered execution preflight failed. Missing or unavailable targets: " +
								missingTargetsJoined +
								". Missing runtime tool IDs: " + missingResolvedJoined +
								". Remediation: verify required runtime manifests/scripts are present and enabled for each target, or remove unavailable targets from the strict ordered sequence.";

							++model.orderedPreflightMissingTargetTotal;

							RunLoopBudget orderedRecoveryBudget;
							const RecoveryOutcome orderedRecoveryOutcome =
								RecoveryPolicyEngine::Execute(
									RecoveryRequest{
										.runId = runId,
										.sessionKey = sessionKey,
										.message = normalizedMessage,
										.errorCode = strictMissingErrorCode,
										.errorMessage = strictMissingErrorMessage,
										.authProfileId = "default",
										.taskDeltas = orderedPreflightTaskDeltas,
									},
									orderedRecoveryBudget);

							EmitTelemetryEvent(
								"gateway.chat.policy.decision",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"layer\":\"ordered_preflight\",\"reason\":\"strict_missing\",\"missingTargets\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
								",\"missingRuntimeToolIds\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingResolvedToolTargets) +
								",\"recoveryRoute\":" +
								JsonString(orderedRecoveryOutcome.recoveryRoute) + "}");
							EmitTelemetryEvent(
								"ordered_preflight_missing_target_total",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"total\":" +
								std::to_string(model.orderedPreflightMissingTargetTotal) +
								",\"missingTargets\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
								",\"missingRuntimeToolIds\":" +
								SerializeStringArrayLocal(orderedSequencePreflight.missingResolvedToolTargets) +
								"}");

							if (orderedRecoveryOutcome.recovered ||
								orderedRecoveryOutcome.shouldReinvokeRuntime) {
								if (!orderedRecoveryOutcome.recoveryDeltas.empty()) {
									orderedPreflightTaskDeltas.insert(
										orderedPreflightTaskDeltas.end(),
										orderedRecoveryOutcome.recoveryDeltas.begin(),
										orderedRecoveryOutcome.recoveryDeltas.end());
								}
								if (!orderedRecoveryOutcome.normalizedDeltas.empty()) {
									orderedPreflightTaskDeltas = orderedRecoveryOutcome.normalizedDeltas;
								}
							}
							else {
								failed = true;
								orchestrationHandled = true;
								backendErrorCode = strictMissingErrorCode;
								backendErrorMessage = strictMissingErrorMessage;
								backendErrorContextJson = JsonObject({
									{"layer", JsonString("ordered_preflight")},
									{"missingOrderedTargets", SerializeStringArrayLocal(orderedSequencePreflight.missingTargets)},
									{"missingRuntimeToolIds", SerializeStringArrayLocal(orderedSequencePreflight.missingResolvedToolTargets)},
									{"remediation", JsonString("verify manifests/scripts exist and tools are enabled, or remove unavailable targets from strict ordered sequence")},
									{"remediationOptions", JsonArray({
										JsonString("verify_runtime_manifests_and_enable_tools"),
										JsonString("remove_unavailable_target_from_strict_sequence"),
										})},
									{"strictAllowlist", JsonBool(orderedSequencePreflight.strictAllowlist)},
									{"orderedTargets", SerializeStringArrayLocal(orderedSequencePreflight.orderedTargets)},
									});
								assistantText =
									(preferChineseResponse
										? (Utf8LiteralLocal(u8"\u65E0\u6CD5\u6267\u884C\u6709\u5E8F\u5DE5\u4F5C\u6D41\uFF0C\u4EE5\u4E0B\u6B65\u9AA4\u76EE\u6807\u7F3A\u5931\u6216\u4E0D\u53EF\u7528\uFF1A") +
											missingTargetsJoined +
											Utf8LiteralLocal(u8"\u3002\u7F3A\u5931 runtime tool ID\uFF1A") +
											missingResolvedJoined +
											Utf8LiteralLocal(u8"\u3002\u5904\u7F6E\u5EFA\u8BAE\uFF1A\u786E\u8BA4 manifest/scripts \u5B58\u5728\u4E14\u5DE5\u5177\u5DF2\u542F\u7528\uFF1B\u82E5\u6682\u65E0\u6CD5\u63D0\u4F9B\uFF0C\u8BF7\u4ECE strict ordered sequence \u4E2D\u79FB\u9664\u8BE5 target\u3002"))
										: (std::string("Unable to execute the strict ordered workflow because required step targets are missing or unavailable: ") +
											missingTargetsJoined +
											". Missing runtime tool IDs: " + missingResolvedJoined +
											". Remediation: verify manifests/scripts exist and tools are enabled, or remove unavailable targets from the strict ordered sequence."));

								auto blockedTaskDeltas =
									RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
										runId,
										sessionKey,
										orderedSequencePreflight,
										true,
										backendErrorCode,
										backendErrorMessage);
								blockedTaskDeltas = mergeWithPreflightTaskDeltas(
									std::move(blockedTaskDeltas),
									true,
									backendErrorCode,
									backendErrorMessage);
								assistantDeltas =
									buildAssistantDeltasFromTaskDeltas(blockedTaskDeltas);
								if (assistantDeltas.empty()) {
									assistantDeltas.push_back(assistantText);
								}

								appendForcedTerminalTaskDeltaIfMissing(
									blockedTaskDeltas,
									runId,
									sessionKey,
									"failed",
									backendErrorCode.empty()
									? "ordered_preflight_terminal_guard_triggered"
									: backendErrorCode,
									backendErrorMessage.empty()
									? "Forced terminal fallback emitted by ordered preflight guard."
									: backendErrorMessage);
								++model.orderedPreflightMissingTargetTerminalEmittedTotal;
								EmitTelemetryEvent(
									"ordered_preflight_missing_target_terminal_emitted_total",
									std::string("{\"runId\":") + JsonString(runId) +
									",\"total\":" +
									std::to_string(model.orderedPreflightMissingTargetTerminalEmittedTotal) +
									",\"errorCode\":" + JsonString(backendErrorCode) +
									",\"missingOrderedTargets\":" +
									SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
									",\"missingRuntimeToolIds\":" +
									SerializeStringArrayLocal(orderedSequencePreflight.missingResolvedToolTargets) +
									"}");

								persistTaskDeltas(blockedTaskDeltas, false);
							}
						}
						else if (!orderedSequencePreflight.missingTargets.empty()) {
							BranchDecisionDiagnostics::Emit(
								runId,
								"runtime",
								"ordered_preflight",
								"advisory_missing_targets_continue");
							EmitTelemetryEvent(
								"gateway.chat.ordered.preflight",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"advisoryContinue\":true,\"missing\":" +
								SerializeStringArrayLocal(
									orderedSequencePreflight.missingTargets) +
								"}");
						}
					}

					if (!forceError &&
						!orchestrationHandled &&
						!hasAttachments &&
						forceWeatherEmailDeterministicOrchestration) {
						const auto orchestrationResult =
							TryOrchestrateWeatherEmailPrompt(
								*runtime.toolRegistry,
								normalizedMessage);

						if (orchestrationResult.matched) {
							orchestrationHandled = true;
							assistantDeltas = orchestrationResult.assistantDeltas;
							if (orchestrationResult.success) {
								failed = false;
								backendErrorCode.clear();
								backendErrorMessage.clear();
								assistantText = orchestrationResult.assistantText;
								if (assistantText.empty() &&
									!assistantDeltas.empty()) {
									assistantText = assistantDeltas.back();
								}
								terminalState = orchestrationResult.terminalStatus == "needs_approval"
									? "needs_approval"
									: "final";
								approvalRequired = orchestrationResult.requiresApproval;
								approvalToken = orchestrationResult.approvalToken;
								approvalTokenExpiresAtEpochMs = orchestrationResult.approvalTokenExpiresAtEpochMs;
								approvalNextAction = orchestrationResult.approvalNextAction;
								terminalReason = orchestrationResult.terminalReason;

								EmitTelemetryEvent(
									"gateway.chat.orchestration.execution",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"path\":" +
									JsonString(orchestrationPath) +
									",\"status\":\"success\",\"steps\":" +
									std::to_string(
										orchestrationResult.decompositionSteps) +
									",\"terminalStatus\":" + JsonString(orchestrationResult.terminalStatus) +
									",\"requiresApproval\":" + std::string(orchestrationResult.requiresApproval ? "true" : "false") +
									"}");

								auto orchestrationTaskDeltas =
									RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
										{},
										runId,
										sessionKey,
										true,
										assistantText,
										{},
										{},
										orchestrationResult.terminalStatus);
								if (!orderedPreflightTaskDeltas.empty()) {
									std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
									mergedTaskDeltas.reserve(
										orderedPreflightTaskDeltas.size() +
										orchestrationTaskDeltas.size());
									mergedTaskDeltas.insert(
										mergedTaskDeltas.end(),
										orderedPreflightTaskDeltas.begin(),
										orderedPreflightTaskDeltas.end());
									mergedTaskDeltas.insert(
										mergedTaskDeltas.end(),
										orchestrationTaskDeltas.begin(),
										orchestrationTaskDeltas.end());
									orchestrationTaskDeltas = std::move(mergedTaskDeltas);
								}

								persistTaskDeltas(orchestrationTaskDeltas, true);
							}
							else {
								failed = true;
								terminalState = "error";
								approvalRequired = false;
								approvalToken.clear();
								approvalTokenExpiresAtEpochMs = 0;
								approvalNextAction.clear();
								terminalReason = orchestrationResult.terminalReason;
								assistantText.clear();
								backendErrorCode = orchestrationResult.errorCode.empty()
									? "chat_tool_orchestration_failed"
									: orchestrationResult.errorCode;
								backendErrorMessage = orchestrationResult.errorMessage.empty()
									? "chat tool orchestration failed"
									: orchestrationResult.errorMessage;

								EmitTelemetryEvent(
									"gateway.chat.orchestration.execution",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"path\":" +
									JsonString(orchestrationPath) +
									",\"status\":\"failed\",\"errorCode\":" +
									JsonString(backendErrorCode) +
									",\"errorMessage\":" +
									JsonString(backendErrorMessage) +
									"}");

								auto orchestrationTaskDeltas =
									RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
										{},
										runId,
										sessionKey,
										false,
										{},
										backendErrorCode,
										backendErrorMessage);
								if (!orderedPreflightTaskDeltas.empty()) {
									std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
									mergedTaskDeltas.reserve(
										orderedPreflightTaskDeltas.size() +
										orchestrationTaskDeltas.size());
									mergedTaskDeltas.insert(
										mergedTaskDeltas.end(),
										orderedPreflightTaskDeltas.begin(),
										orderedPreflightTaskDeltas.end());
									mergedTaskDeltas.insert(
										mergedTaskDeltas.end(),
										orchestrationTaskDeltas.begin(),
										orchestrationTaskDeltas.end());
									orchestrationTaskDeltas = std::move(mergedTaskDeltas);
								}

								persistTaskDeltas(orchestrationTaskDeltas, false);
							}
						}
					}

					if (!forceError && !orchestrationHandled && callbacks.chatRuntimeCallback) {
						auto& runtimeSessionEvents = sessions.eventsBySession[sessionKey];
						PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.approvalRequired = false,
								.approvalToken = std::nullopt,
								.approvalTokenExpiresAtEpochMs = std::nullopt,
								.approvalNextAction = std::nullopt,
								.terminalReason = std::nullopt,
								.timestampMs = nowMs,
							});
						PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.approvalRequired = false,
								.approvalToken = std::nullopt,
								.approvalTokenExpiresAtEpochMs = std::nullopt,
								.approvalNextAction = std::nullopt,
								.terminalReason = std::nullopt,
								.timestampMs = nowMs,
							});
						lifecycleEventsEnqueued = true;
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"queued",
							runId,
							sessionKey,
							nowMs);
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"started",
							runId,
							sessionKey,
							nowMs);
						if (pushLifecycleEnabled) {
							EmitPushLifecycleEvent(
								*runtime.transport,
								*runtime.eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "queued",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								sessions.pushEventSeq);
							EmitPushLifecycleEvent(
								*runtime.transport,
								*runtime.eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "started",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								sessions.pushEventSeq);
						}

						run.runsById.insert_or_assign(
							runId,
							GatewayHost::ChatRunState{
								.runId = runId,
								.sessionKey = sessionKey,
								.idempotencyKey = idempotencyKey,
								.userMessage = message,
								.assistantText = {},
								.providerDeltas = {},
								.providerDeltaCursor = 0,
								.streamCursor = 0,
								.lastEmitMs = nowMs,
								.lastProgressAtMs = nowMs,
								.terminalWaitExceededNotified = false,
								.failed = false,
								.terminalState = "final",
								.approvalRequired = false,
								.approvalToken = {},
								.approvalTokenExpiresAtEpochMs = 0,
								.approvalNextAction = {},
								.terminalReason = {},
								.errorCode = {},
								.errorMessage = {},
								.errorContextJson = {},
								.startedAtMs = nowMs,
								.active = true,
								.detached = detachedSend,
								.terminalEventEnqueued = false,
								.pushLifecycleRequested = pushLifecycleEnabled,
								.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
								.originatingChannel = sendControlDecision.route.originatingChannel,
								.originatingTo = sendControlDecision.route.originatingTo,
								.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
								.inputSource = hasTranscriptInjection ? transcriptSource : "typed",
								.voiceTranscriptInjected = hasTranscriptInjection,
								.transcriptSessionId = transcriptSessionId,
								.transcriptRunId = transcriptRunId,
								.transcriptInjectionJson = hasTranscriptInjection ? transcriptInjectionRaw : std::string(),
								.speechArtifactJson = hasSpeechArtifact ? speechArtifactRaw : std::string(),
							});

						std::size_t streamedDeltaCount = 0;
						const auto runtimeResult = callbacks.chatRuntimeCallback(
							GatewayHost::ChatRuntimeRequest{
								.runId = runId,
								.sessionKey = sessionKey,
								.message = runtimeMessage,
								.bodyForCommands = stageContext.bodyForCommands,
								.bodyForAgent = stageContext.bodyForAgent.empty()
									? runtimeMessage
									: stageContext.bodyForAgent,
								.modelIdOverride = effectiveRequestedModelOverride,
								.providerOverride = effectiveRequestedProviderOverride,
								.slashCommandName = stageContext.slashCommandName,
								.shouldLoadInlineSkillCommands =
									stageContext.shouldLoadInlineSkillCommands,
								.inlineInvocationAuthorizedSender =
									stageContext.inlineInvocationAuthorizedSender,
								.inlineInvocationSenderIsOwner =
									stageContext.inlineInvocationSenderIsOwner,
								.allowInlineToolImmediateExecution =
									stageContext.allowInlineToolImmediateExecution,
								.enforceOrderedAllowlist = enforceOrderedAllowlist,
								.orderedAllowedToolTargets = orderedAllowlistTargets,
								.hasAttachments = hasAttachments,
								.attachmentMimeTypes = attachmentMimeTypes,
								.onAssistantDelta =
									[&run, &sessions, &runtime,
										&streamedDeltaCount,
										&runId,
										&sessionKey,
										&controlPlaneService,
										&sendControlDecision](const std::string& delta) {
										const std::string normalizedDelta = json::Trim(delta);
										if (normalizedDelta.empty() ||
											RuntimeTranscriptGuard::IsSilentReplyText(normalizedDelta)) {
											return;
										}

										if (!controlPlaneService.ShouldPublishToolDelta(
											normalizedDelta,
											sendControlDecision)) {
											return;
										}
										if (normalizedDelta.find("tools.execute") == 0) {
											const auto recipientsIt =
												run.toolEventRecipientsByRun.find(runId);
											if (recipientsIt == run.toolEventRecipientsByRun.end() ||
												recipientsIt->second.empty()) {
												return;
											}
										}

										auto& streamEvents = sessions.eventsBySession[sessionKey];
										PushEventWithRetentionLimit(streamEvents, GatewayHost::ChatEventState{
												.runId = runId,
												.sessionKey = sessionKey,
												.state = "delta",
												.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
												.errorMessage = std::nullopt,
												.approvalRequired = false,
												.approvalToken = std::nullopt,
												.approvalTokenExpiresAtEpochMs = std::nullopt,
												.approvalNextAction = std::nullopt,
												.terminalReason = std::nullopt,
												.timestampMs = CurrentEpochMsLocal(),
											});
										const std::uint64_t deltaNowMs = CurrentEpochMsLocal();
										GatewayLifecycleEventEmitter::EmitLifecycle(
											"delta",
											runId,
											sessionKey,
										 deltaNowMs);

										auto runStateIt = run.runsById.find(runId);
										if (runStateIt != run.runsById.end() &&
											runStateIt->second.pushLifecycleRequested) {
											EmitPushLifecycleEvent(
												*runtime.transport,
												*runtime.eventFanout,
												GatewayEventFanoutService::ChatLifecycleEvent{
													.runId = runId,
													.sessionKey = sessionKey,
													.state = "delta",
													.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
													.errorMessage = std::nullopt,
													.timestampMs = deltaNowMs,
												},
												sessions.pushEventSeq);
										}

										if (runStateIt != run.runsById.end()) {
											runStateIt->second.assistantText = normalizedDelta;
											runStateIt->second.streamCursor = normalizedDelta.size();
											runStateIt->second.lastEmitMs = deltaNowMs;
											runStateIt->second.lastProgressAtMs = deltaNowMs;
											runStateIt->second.terminalWaitExceededNotified = false;
										}

										++streamedDeltaCount;
									}
							});
						providerStreamed = streamedDeltaCount > 0;

						if (runtimeResult.ok) {
							if (!runtimeResult.assistantText.empty()) {
								assistantText = runtimeResult.assistantText;
							}

							if (assistantText.empty() &&
								!runtimeResult.assistantDeltas.empty()) {
								assistantText = runtimeResult.assistantDeltas.back();
							}

							std::vector<std::string> providerDeltas =
								RuntimeTranscriptGuard::NormalizeAssistantDeltas(
									runtimeResult.assistantDeltas,
									assistantText,
									providerStreamed);
							assistantDeltas = providerDeltas;

							if (assistantText.empty()) {
								failed = true;
								backendErrorCode = "chat_runtime_empty_response";
								backendErrorMessage =
									"chat runtime returned no assistant output";
							}
							else {
								auto existingRunIt = run.runsById.find(runId);
								if (existingRunIt != run.runsById.end()) {
									existingRunIt->second.assistantText = assistantText;
									existingRunIt->second.providerDeltas = assistantDeltas;
									existingRunIt->second.providerDeltaCursor = 0;
									existingRunIt->second.streamCursor =
										providerStreamed ? assistantText.size() : 0;
									existingRunIt->second.lastEmitMs = nowMs;
									existingRunIt->second.lastProgressAtMs = nowMs;
									existingRunIt->second.terminalWaitExceededNotified = false;
									existingRunIt->second.failed = failed;
									existingRunIt->second.terminalState = failed ? "error" : "final";
									existingRunIt->second.approvalRequired = false;
									existingRunIt->second.approvalToken.clear();
									existingRunIt->second.approvalTokenExpiresAtEpochMs = 0;
									existingRunIt->second.approvalNextAction.clear();
									existingRunIt->second.terminalReason.clear();
									existingRunIt->second.errorCode = backendErrorCode;
									existingRunIt->second.errorMessage = backendErrorMessage;
									existingRunIt->second.errorContextJson = backendErrorContextJson;
									existingRunIt->second.active = true;
								}
							}
						}
						else {
							failed = true;
							backendErrorCode = runtimeResult.errorCode.empty()
								? "chat_runtime_error"
								: runtimeResult.errorCode;
							backendErrorMessage = runtimeResult.errorMessage.empty()
								? "chat runtime failed"
								: runtimeResult.errorMessage;
							if (backendErrorMessage.find("baidu-search.search.web") != std::string::npos &&
								(backendErrorMessage.find("429") != std::string::npos ||
									backendErrorCode.find("rate_limit") != std::string::npos)) {
								backendErrorContextJson = JsonObject({
									{"layer", JsonString("tool_runtime")},
									{"toolId", JsonString("baidu-search.search.web")},
									{"errorCategory", JsonString("rate_limited")},
									{"fallbackInstruction", JsonString("retry later, reduce burst frequency, or use fallback search source")},
									{"cooldownSuggestion", JsonString("wait for cooldown window and avoid immediate repeated identical queries")},
									});
							}
							assistantText.clear();
						}

						auto existingRunIt = run.runsById.find(runId);
						if (existingRunIt != run.runsById.end()) {
							existingRunIt->second.assistantText = assistantText;
							existingRunIt->second.providerDeltas = assistantDeltas;
							existingRunIt->second.providerDeltaCursor = 0;
							existingRunIt->second.streamCursor =
								providerStreamed ? assistantText.size() : 0;
							existingRunIt->second.lastEmitMs = nowMs;
							existingRunIt->second.lastProgressAtMs = nowMs;
							existingRunIt->second.terminalWaitExceededNotified = false;
							existingRunIt->second.failed = failed;
							existingRunIt->second.terminalState = failed ? "error" : "final";
							existingRunIt->second.approvalRequired = false;
							existingRunIt->second.approvalToken.clear();
							existingRunIt->second.approvalTokenExpiresAtEpochMs = 0;
							existingRunIt->second.approvalNextAction.clear();
							existingRunIt->second.terminalReason.clear();
							existingRunIt->second.errorCode = backendErrorCode;
							existingRunIt->second.errorMessage = backendErrorMessage;
							existingRunIt->second.errorContextJson = backendErrorContextJson;
							existingRunIt->second.active = true;
						}

						auto runtimeTaskDeltas =
							RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
								runtimeResult.taskDeltas,
								runId,
								sessionKey,
								runtimeResult.ok && !failed,
								assistantText,
								backendErrorCode,
								backendErrorMessage);
						runtimeTaskDeltas =
							RuntimeToolCallNormalizer::ApplyInvalidArgumentsRecoveryPolicy(
								runtimeTaskDeltas,
								runId,
								sessionKey,
								normalizedMessage,
								*runtime.toolRegistry);

						if (failed) {
							RunLoopBudget budget;
							const RecoveryOutcome recoveryOutcome =
								RecoveryPolicyEngine::Execute(
									RecoveryRequest{
										.runId = runId,
										.sessionKey = sessionKey,
										.message = normalizedMessage,
										.errorCode = backendErrorCode,
										.errorMessage = backendErrorMessage,
										.authProfileId = "default",
										.taskDeltas = runtimeTaskDeltas,
									},
									budget);

							if (!recoveryOutcome.recoveryDeltas.empty()) {
								runtimeTaskDeltas.insert(
									runtimeTaskDeltas.end(),
									recoveryOutcome.recoveryDeltas.begin(),
									recoveryOutcome.recoveryDeltas.end());
							}

							if (!recoveryOutcome.normalizedDeltas.empty()) {
								runtimeTaskDeltas = recoveryOutcome.normalizedDeltas;
							}

							EmitTelemetryEvent(
								"gateway.chat.recovery.decision",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"recovered\":" +
								std::string(recoveryOutcome.recovered ? "true" : "false") +
								",\"retry\":" +
								std::string(recoveryOutcome.shouldRetry ? "true" : "false") +
								",\"reinvoke\":" +
								std::string(recoveryOutcome.shouldReinvokeRuntime ? "true" : "false") +
								",\"recoveryRoute\":" +
								JsonString(recoveryOutcome.recoveryRoute) +
								",\"compaction\":" +
								std::string(recoveryOutcome.compactionApplied ? "true" : "false") +
								",\"truncation\":" +
								std::string(recoveryOutcome.truncationApplied ? "true" : "false") +
								",\"fallbackPolicyProfile\":" +
								JsonString(orchestrationPolicy.fallbackPolicyProfile) +
								",\"profile\":" +
								JsonString(recoveryOutcome.selectedProfileId) +
								",\"contextEngine\":" +
								JsonString(recoveryOutcome.selectedContextEngineId) +
								",\"terminalCode\":" +
								JsonString(recoveryOutcome.terminalErrorCode) +
								"}");
							BranchDecisionDiagnostics::Emit(
								runId,
								"recovery",
								recoveryOutcome.recovered
								? "recovered"
								: "terminal",
								recoveryOutcome.terminalErrorCode.empty()
								? "recovery_chain_continue"
								: recoveryOutcome.terminalErrorCode);

							if (recoveryOutcome.recovered) {
								failed = false;
								backendErrorCode.clear();
								backendErrorMessage.clear();
								if (assistantText.empty()) {
									assistantText = recoveryOutcome.compactionApplied
										? "Recovered via context compaction; runtime will continue."
										: "Recovered via fallback normalization; runtime will continue.";
								}
							}
							else if (!recoveryOutcome.terminalErrorCode.empty()) {
								backendErrorCode = recoveryOutcome.terminalErrorCode;
								if (!recoveryOutcome.terminalErrorMessage.empty()) {
									backendErrorMessage = recoveryOutcome.terminalErrorMessage;
								}
							}
						}

						runtimeTaskDeltas = mergeWithPreflightTaskDeltas(
							std::move(runtimeTaskDeltas),
							failed,
							backendErrorCode,
							backendErrorMessage);

						persistTaskDeltas(
							runtimeTaskDeltas,
							runtimeResult.ok && !failed);
					}
					else if (!forceError && !orchestrationHandled && !callbacks.chatRuntimeCallback) {
						failed = true;
						assistantText.clear();
						assistantDeltas.clear();
						backendErrorCode = "chat_runtime_callback_missing";
						backendErrorMessage = "chat runtime callback is not configured";

						auto runtimeTaskDeltas =
							RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
								{},
								runId,
								sessionKey,
								false,
								assistantText,
								backendErrorCode,
								backendErrorMessage);
						runtimeTaskDeltas = mergeWithPreflightTaskDeltas(
							std::move(runtimeTaskDeltas),
							true,
							backendErrorCode,
							backendErrorMessage);

						persistTaskDeltas(runtimeTaskDeltas, false);
					}

					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(assistantText);
					if (!assistantText.empty() && !silentAssistantReply) {
						const auto assistantPersisted = transcriptStore.AppendAssistantMessage(
							ChatTranscriptStore::AppendParams{
								.sessionKey = sessionKey,
								.role = "assistant",
								.message = assistantText,
								.label = std::string(),
								.idempotencyKey = runId + ":assistant",
							});
						if (!assistantPersisted.ok && !assistantPersisted.error.empty()) {
							EmitTelemetryEvent(
								"gateway.chat.transcript.assistant.persist.error",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"sessionKey\":" + JsonString(sessionKey) +
								",\"error\":" + JsonString(assistantPersisted.error) + "}");
						}
					}

					auto& sessionEvents = sessions.eventsBySession[sessionKey];
					if (!lifecycleEventsEnqueued) {
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.approvalRequired = false,
								.approvalToken = std::nullopt,
								.approvalTokenExpiresAtEpochMs = std::nullopt,
								.approvalNextAction = std::nullopt,
								.terminalReason = std::nullopt,
								.timestampMs = nowMs,
							});
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.approvalRequired = false,
								.approvalToken = std::nullopt,
								.approvalTokenExpiresAtEpochMs = std::nullopt,
								.approvalNextAction = std::nullopt,
								.terminalReason = std::nullopt,
								.timestampMs = nowMs,
							});
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"queued",
							runId,
							sessionKey,
							nowMs);
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"started",
							runId,
							sessionKey,
							nowMs);
						if (pushLifecycleEnabled) {
							EmitPushLifecycleEvent(
								*runtime.transport,
								*runtime.eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "queued",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								sessions.pushEventSeq);
							EmitPushLifecycleEvent(
								*runtime.transport,
								*runtime.eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "started",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								sessions.pushEventSeq);
						}
					}
					EmitDeepSeekGatewayDiagnostic(
						"event.enqueue",
						std::string("state=queued runId=") +
						runId +
						" session=" +
						sessionKey +
						" queueSize=" +
						std::to_string(sessionEvents.size()));
					EmitDeepSeekGatewayDiagnostic(
						"event.enqueue",
						std::string("state=started runId=") +
						runId +
						" session=" +
						sessionKey +
						" queueSize=" +
						std::to_string(sessionEvents.size()));

					std::size_t streamCursor = 0;
					if (!failed && !silentAssistantReply && !providerStreamed) {
						const auto emitAssistantDeltaChunk = [&](
							const std::string& chunk,
							std::size_t cursorAfter) {
								if (chunk.empty()) {
									return;
								}
								streamCursor = cursorAfter;
								const std::string deltaMessage = BuildAssistantDeltaMessageJson(chunk);
								PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "delta",
									.messageJson = deltaMessage,
									.errorMessage = std::nullopt,
									.approvalRequired = false,
									.approvalToken = std::nullopt,
									.approvalTokenExpiresAtEpochMs = std::nullopt,
									.approvalNextAction = std::nullopt,
									.terminalReason = std::nullopt,
									.timestampMs = nowMs,
									});
								GatewayLifecycleEventEmitter::EmitLifecycle(
									"delta",
									runId,
									sessionKey,
									nowMs);
								EmitDeepSeekGatewayDiagnostic(
									"event.enqueue",
									std::string("state=delta runId=") +
									runId +
									" session=" +
									sessionKey +
									" queueSize=" +
									std::to_string(sessionEvents.size()));
								if (pushLifecycleEnabled) {
									EmitPushLifecycleEvent(
										*runtime.transport,
										*runtime.eventFanout,
										GatewayEventFanoutService::ChatLifecycleEvent{
											.runId = runId,
											.sessionKey = sessionKey,
											.state = "delta",
											.messageJson = deltaMessage,
											.errorMessage = std::nullopt,
											.timestampMs = nowMs,
										},
										sessions.pushEventSeq);
								}
							};

						if (!assistantDeltas.empty()) {
							// Tool-heavy orchestration responses are already fully computed.
							// Emit full text immediately to avoid prolonged synthetic reveal loops.
							const bool hasToolLifecycleDelta = std::any_of(
								assistantDeltas.begin(),
								assistantDeltas.end(),
								[](const std::string& delta) {
									const auto firstNonSpace = std::find_if_not(
										delta.begin(),
										delta.end(),
										[](unsigned char ch) {
											return std::isspace(ch) != 0;
										});
									if (firstNonSpace == delta.end()) {
										return false;
									}
									const std::string normalized(firstNonSpace, delta.end());
									return normalized.find("tools.execute.") == 0;
								});
							if (hasToolLifecycleDelta) {
								EmitTelemetryEvent(
									"gateway.chat.send.tool_heavy_direct_emit",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"sessionKey\":" +
									JsonString(sessionKey) +
									",\"assistantTextBytes\":" +
									std::to_string(assistantText.size()) +
									",\"providerDeltaCount\":" +
									std::to_string(assistantDeltas.size()) +
									"}");
								for (const auto& providerDelta : assistantDeltas) {
									if (providerDelta.empty()) {
										continue;
									}

									const auto firstNonSpace = std::find_if_not(
										providerDelta.begin(),
										providerDelta.end(),
										[](unsigned char ch) {
											return std::isspace(ch) != 0;
										});
									if (firstNonSpace == providerDelta.end()) {
										continue;
									}

									const std::string normalized(
										firstNonSpace,
										providerDelta.end());
									if (normalized.rfind("tools.execute.", 0) == 0 ||
										normalized.rfind("orchestration.", 0) == 0) {
										emitAssistantDeltaChunk(
											providerDelta,
											streamCursor + providerDelta.size());
									}
								}

								if (!assistantText.empty()) {
									emitAssistantDeltaChunk(
										assistantText,
										assistantText.size());
								}
							}
							else {
								bool emittedIncrementalProviderDeltas = false;
								std::size_t incrementalCursor = 0;
								for (const auto& providerDelta : assistantDeltas) {
									if (providerDelta.empty()) {
										continue;
									}
									if (incrementalCursor + providerDelta.size() > assistantText.size()) {
										emittedIncrementalProviderDeltas = false;
										incrementalCursor = 0;
										break;
									}
									const std::string expectedChunk = assistantText.substr(incrementalCursor, providerDelta.size());
									if (expectedChunk != providerDelta) {
										emittedIncrementalProviderDeltas = false;
										incrementalCursor = 0;
										break;
									}
									incrementalCursor += providerDelta.size();
									emitAssistantDeltaChunk(providerDelta, incrementalCursor);
									emittedIncrementalProviderDeltas = true;
								}
								if (!emittedIncrementalProviderDeltas || incrementalCursor == 0) {
									const std::size_t n =
										(std::min)(assistantText.size(), std::size_t{ 64 });
									emitAssistantDeltaChunk(assistantText.substr(0, n), n);
									EmitTelemetryEvent(
										"gateway.chat.send.synthetic_fallback",
										std::string("{\"runId\":") + JsonString(runId) +
										",\"sessionKey\":" + JsonString(sessionKey) +
										",\"reason\":\"provider_deltas_not_prefix_consistent\"}");
								}
							}
						}
						else if (!assistantText.empty()) {
							// Phase D: no incremental provider stream — emit one assistant delta with
							// full text instead of synthetic 6-char + poll-simulated streaming.
							emitAssistantDeltaChunk(assistantText, assistantText.size());
						}
					}

					if (run.runsById.find(runId) == run.runsById.end()) {
						run.runsById.insert_or_assign(
							runId,
							GatewayHost::ChatRunState{
								.runId = runId,
								.sessionKey = sessionKey,
								.idempotencyKey = idempotencyKey,
								.userMessage = message,
								.assistantText = assistantText,
								.providerDeltas = assistantDeltas,
								.providerDeltaCursor = 0,
								.streamCursor = streamCursor,
								.lastEmitMs = nowMs,
								.lastProgressAtMs = nowMs,
								.terminalWaitExceededNotified = false,
								.failed = failed,
								.terminalState = failed
									? "error"
									: (terminalState.empty() ? "final" : terminalState),
								.approvalRequired = approvalRequired,
								.approvalToken = approvalToken,
								.approvalTokenExpiresAtEpochMs = approvalTokenExpiresAtEpochMs,
								.approvalNextAction = approvalNextAction,
								.terminalReason = terminalReason,
								.errorCode = backendErrorCode,
								.errorMessage = backendErrorMessage,
								.errorContextJson = backendErrorContextJson,
								.startedAtMs = nowMs,
								.active = true,
								.detached = detachedSend,
								.terminalEventEnqueued = false,
								.pushLifecycleRequested = pushLifecycleEnabled,
								.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
								.originatingChannel = sendControlDecision.route.originatingChannel,
								.originatingTo = sendControlDecision.route.originatingTo,
								.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
								.inputSource = hasTranscriptInjection ? transcriptSource : "typed",
								.voiceTranscriptInjected = hasTranscriptInjection,
								.transcriptSessionId = transcriptSessionId,
								.transcriptRunId = transcriptRunId,
								.transcriptInjectionJson = hasTranscriptInjection ? transcriptInjectionRaw : std::string(),
								.speechArtifactJson = hasSpeechArtifact ? speechArtifactRaw : std::string(),
							});
					}
					auto insertedRunIt = run.runsById.find(runId);
					if (insertedRunIt != run.runsById.end() &&
						!insertedRunIt->second.failed &&
						!silentAssistantReply &&
						insertedRunIt->second.streamCursor >= insertedRunIt->second.assistantText.size() &&
						!insertedRunIt->second.terminalEventEnqueued) {
						const std::string resolvedTerminalState =
							insertedRunIt->second.terminalState.empty()
							? "final"
							: insertedRunIt->second.terminalState;
						const std::optional<std::string> terminalMessage =
							std::optional<std::string>(
								BuildAssistantFinalMessageJson(insertedRunIt->second.assistantText, nowMs));
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							.runId = insertedRunIt->second.runId,
							.sessionKey = insertedRunIt->second.sessionKey,
							.state = resolvedTerminalState,
							.messageJson = terminalMessage,
							.errorMessage = std::nullopt,
							.approvalRequired = resolvedTerminalState == "needs_approval"
								? insertedRunIt->second.approvalRequired
								: false,
							.approvalToken = (resolvedTerminalState == "needs_approval" &&
								!insertedRunIt->second.approvalToken.empty())
								? std::optional<std::string>(insertedRunIt->second.approvalToken)
								: std::nullopt,
							.approvalTokenExpiresAtEpochMs =
								(resolvedTerminalState == "needs_approval" &&
									insertedRunIt->second.approvalTokenExpiresAtEpochMs > 0)
								? std::optional<std::uint64_t>(insertedRunIt->second.approvalTokenExpiresAtEpochMs)
								: std::nullopt,
							.approvalNextAction = (resolvedTerminalState == "needs_approval" &&
								!insertedRunIt->second.approvalNextAction.empty())
								? std::optional<std::string>(insertedRunIt->second.approvalNextAction)
								: std::nullopt,
							.terminalReason = insertedRunIt->second.terminalReason.empty()
								? std::nullopt
								: std::optional<std::string>(insertedRunIt->second.terminalReason),
							.timestampMs = nowMs,
							});
						GatewayLifecycleEventEmitter::EmitLifecycle(
							resolvedTerminalState,
							insertedRunIt->second.runId,
							insertedRunIt->second.sessionKey,
							nowMs);
						if (insertedRunIt->second.pushLifecycleRequested) {
							EmitPushLifecycleEvent(
								*runtime.transport,
								*runtime.eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = insertedRunIt->second.runId,
									.sessionKey = insertedRunIt->second.sessionKey,
									.state = resolvedTerminalState,
									.messageJson = terminalMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								sessions.pushEventSeq);
						}
						insertedRunIt->second.terminalEventEnqueued = true;
						insertedRunIt->second.active = false;
						runtime.transportRecipientRegistry->MarkRunFinalized(
							insertedRunIt->second.runId,
							nowMs);
						run.toolEventRecipientsByRun.erase(insertedRunIt->second.runId);
						runtime.transportRecipientRegistry->PruneExpired(nowMs);
						EmitTelemetryEvent(
							"gateway.chat.final.fastpath",
							std::string("{\"runId\":") +
							JsonString(insertedRunIt->second.runId) +
							",\"sessionKey\":" +
							JsonString(insertedRunIt->second.sessionKey) +
							",\"terminalState\":" + JsonString(resolvedTerminalState) +
							",\"reason\":\"assistant_text_fully_available\"}");
					}

					run.terminalDeliveredRunIds.erase(runId);

					if (!idempotencyKey.empty()) {
						run.runByIdempotency.insert_or_assign(idempotencyKey, runId);
					}

					std::string sendPayload =
						"{\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"promptRunId\":" +
						JsonString(runId) +
						",\"responders\":" +
						responderManifestJson +
						",\"backendErrorCode\":" +
						(backendErrorCode.empty()
							? std::string("null")
							: ("\"" + EscapeJsonLocal(backendErrorCode) + "\"")) +
						",\"queued\":true,\"deduped\":false" +
						",\"originatingChannel\":" +
						JsonString(sendControlDecision.route.originatingChannel) +
						",\"explicitDeliverRoute\":" +
						std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false");

					if (stageContext.pushLifecycleRequested) {
						sendPayload +=
							",\"lifecycle\":{\"transport\":\"push_compatible\",\"state\":\"started\"}";
					}
					sendPayload += "}";

					const protocol::ResponseFrame sendResponse = protocol::OkResponse(request, sendPayload);
					if (!idempotencyKey.empty()) {
						run.replayByIdempotency.insert_or_assign(
							idempotencyKey,
							GatewayHost::ChatReplayEntry{
								.ok = sendResponse.ok,
								.payloadJson = sendResponse.payloadJson,
								.error = sendResponse.error,
							});
					}

					if (stageContext.pushLifecycleRequested) {
						EmitTelemetryEvent(
							"gateway.chat.lifecycle.push_ack",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"sessionKey\":" + JsonString(sessionKey) +
							",\"state\":\"started\"}");
					}

					return sendResponse;
				});

			dispatcher->Register(
				"chat.inject",
				[sessions](const protocol::RequestFrame& request) {
					const auto route =
						ChatPipelineRequestNormalization::NormalizeInjectRoute(request.paramsJson);
					const std::string& sessionKey = route.session.sessionKey;
					const std::string& message = route.message;
					const std::string& label = route.label;

					if (json::Trim(message).empty()) {
						return protocol::ErrorResponse(
							request,
							BuildRuntimeErrorShape(
								"invalid_params",
								"`message` must be a non-empty string.",
								request.id,
								sessionKey));
					}

					if (message.size() > kMaxChatUserMessageUtf8Bytes) {
						return protocol::ErrorResponse(
							request,
							BuildRuntimeErrorShape(
								"message_too_large",
								"chat.inject message exceeds maximum size (" +
								std::to_string(kMaxChatUserMessageUtf8Bytes) +
								" UTF-8 bytes).",
								request.id,
								sessionKey));
					}

					const ChatTranscriptStore transcriptStore;
					const auto appended = transcriptStore.AppendAssistantMessage(
						ChatTranscriptStore::AppendParams{
							.sessionKey = sessionKey,
							.message = message,
							.label = label,
						 .idempotencyKey = request.id,
						});
					if (!appended.ok) {
						return protocol::ErrorResponse(
							request,
							BuildRuntimeErrorShape(
								"unavailable",
								"failed to write transcript: " +
								(appended.error.empty()
									? std::string("unknown error")
									: appended.error),
								request.id,
								sessionKey));
					}

					if (appended.messageId.empty() || appended.messageJson.empty()) {
						return protocol::OkResponse(request, "{\"ok\":true,\"deduped\":true}");
					}

					const std::uint64_t nowMs = CurrentEpochMsLocal();
					const std::string runId = "inject-" + appended.messageId;
					auto& queue = sessions.eventsBySession[sessionKey];
					PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "final",
							.messageJson = appended.messageJson,
							.errorMessage = std::nullopt,
							.approvalRequired = false,
							.approvalToken = std::nullopt,
							.approvalTokenExpiresAtEpochMs = std::nullopt,
							.approvalNextAction = std::nullopt,
							.terminalReason = std::nullopt,
							.timestampMs = nowMs,
						});
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"final",
						runId,
						sessionKey,
						nowMs);

					if (!RuntimeTranscriptGuard::IsSilentReplyText(message)) {
						PushHistoryMessageIfNew(
							sessions.historyBySession[sessionKey],
							appended.messageJson);
					}

					return protocol::OkResponse(request, "{\"ok\":true,\"messageId\":" +
						JsonString(appended.messageId) + "}");
				});

			dispatcher->Register(
				"chat.abort",
				[run, sessions, callbacks, runtime](const protocol::RequestFrame& request) {
					const auto route =
						ChatPipelineRequestNormalization::NormalizeAbortRoute(request.paramsJson);
					const std::string& sessionKey = route.session.sessionKey;
					const std::string& requestedRunId = route.requestedRunId;

					auto runIt = run.runsById.end();
					if (!requestedRunId.empty()) {
						const auto exact = run.runsById.find(requestedRunId);
						if (exact != run.runsById.end() &&
							exact->second.sessionKey == sessionKey) {
							runIt = exact;
						}
					}
					else {
						runIt = std::find_if(
							run.runsById.begin(),
							run.runsById.end(),
							[&](const auto& pair) {
								return pair.second.sessionKey == sessionKey &&
									pair.second.active;
							});
					}

					if (runIt == run.runsById.end()) {
						return protocol::OkResponse(request, "{\"aborted\":false,\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\"}");
					}

					const std::string runId = runIt->second.runId;
					if (callbacks.chatAbortCallback) {
						callbacks.chatAbortCallback(
							GatewayHost::ChatAbortRequest{
								.runId = runId,
								.sessionKey = sessionKey,
							});
					}

					auto& queue = sessions.eventsBySession[sessionKey];
					std::erase_if(
						queue,
						[&](const GatewayHost::ChatEventState& item) {
							return item.runId == runId;
						});

					const std::uint64_t nowMs = CurrentEpochMsLocal();
					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(
							runIt->second.assistantText);
					PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
						   .runId = runIt->second.runId,
						   .sessionKey = sessionKey,
						   .state = "aborted",
						   .messageJson = silentAssistantReply
							   ? std::nullopt
							   : std::optional<std::string>(
								   BuildAssistantFinalMessageJson(
									   runIt->second.assistantText,
									   nowMs)),
						   .errorMessage = std::nullopt,
						   .approvalRequired = false,
						   .approvalToken = std::nullopt,
						   .approvalTokenExpiresAtEpochMs = std::nullopt,
						   .approvalNextAction = std::nullopt,
						   .terminalReason = std::nullopt,
						   .timestampMs = nowMs,
						});
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"aborted",
						runIt->second.runId,
						sessionKey,
						nowMs);
					runIt->second.terminalEventEnqueued = true;
					EmitDeepSeekGatewayDiagnostic(
						"event.enqueue",
						std::string("state=aborted runId=") +
						runIt->second.runId +
						" session=" +
						sessionKey +
						" queueSize=" +
						std::to_string(queue.size()));

					runIt->second.active = false;
					runIt->second.streamCursor = runIt->second.assistantText.size();
					runIt->second.lastProgressAtMs = nowMs;
					run.toolEventRecipientsByRun.erase(runId);
					runtime.transportRecipientRegistry->MarkRunFinalized(runId, nowMs);
					runtime.transportRecipientRegistry->PruneExpired(nowMs);

					ChatAbortCoordinator abortCoordinator;
					const auto persistResult = abortCoordinator.PersistAbortedPartial(
						ChatAbortCoordinator::PersistPartialParams{
							.sessionKey = sessionKey,
							.runId = runId,
							.text = runIt->second.assistantText,
							.origin = "rpc",
						});
					if (!persistResult.persisted && !persistResult.error.empty()) {
						EmitTelemetryEvent(
							"gateway.chat.abort.partialPersistence",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"sessionKey\":" +
							JsonString(sessionKey) +
							",\"persisted\":false,\"error\":" +
							JsonString(persistResult.error) +
							"}");
					}

					return protocol::OkResponse(request, "{\"aborted\":true,\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\",\"partialPersisted\":" +
						std::string(persistResult.persisted ? "true" : "false") +
						"}");
				});

			dispatcher->Register(
				"chat.events.poll",
				[run, sessions, pollMetrics, runtime, fastSyntheticRevealMode, syntheticRevealMaxDurationMs](
					const protocol::RequestFrame& request) {
						const auto route =
							ChatPipelineRequestNormalization::NormalizePollRoute(request.paramsJson);
						const std::string& sessionKey = route.session.sessionKey;
						const std::size_t limit = route.limit;

						const std::uint64_t nowMs = CurrentEpochMsLocal();
						auto& queue = sessions.eventsBySession[sessionKey];
						constexpr std::size_t maxActiveRunsPerPoll = 12;
						constexpr std::uint64_t stalledActiveRunTimeoutMs = 45 * 1000;
						constexpr std::uint64_t terminalWaitExceededThresholdMs = 12 * 1000;

						std::vector<GatewayHost::ChatRunState*> sessionActiveRuns;
						sessionActiveRuns.reserve(run.runsById.size());
						for (auto& [_, candidateRun] : run.runsById) {
							if (candidateRun.sessionKey == sessionKey && candidateRun.active) {
								sessionActiveRuns.push_back(&candidateRun);
							}
						}

						std::sort(
							sessionActiveRuns.begin(),
							sessionActiveRuns.end(),
							[](const GatewayHost::ChatRunState* left, const GatewayHost::ChatRunState* right) {
								const std::uint64_t leftProgress =
									left->lastProgressAtMs > 0 ? left->lastProgressAtMs : left->startedAtMs;
								const std::uint64_t rightProgress =
									right->lastProgressAtMs > 0 ? right->lastProgressAtMs : right->startedAtMs;
								if (leftProgress != rightProgress) {
									return leftProgress < rightProgress;
								}
								return left->startedAtMs > right->startedAtMs;
							});

						const std::size_t reconcileCount =
							(std::min)(sessionActiveRuns.size(), maxActiveRunsPerPoll);
						std::size_t stalledRunCountThisPoll = 0;
						std::string reconcileFocusRunId;
						std::uint64_t reconcileFocusElapsedMs = 0;
						if (sessionActiveRuns.size() > maxActiveRunsPerPoll) {
							EmitTelemetryEvent(
								"gateway.chat.poll.active_run_reconcile_bounded",
								std::string("{\"sessionKey\":") + JsonString(sessionKey) +
								",\"activeRuns\":" + std::to_string(sessionActiveRuns.size()) +
								",\"reconcileCount\":" + std::to_string(reconcileCount) + "}");
						}

						const auto& runTracking = run;
						auto processRun = [&](GatewayHost::ChatRunState& run) {
							const bool pushLifecycleEnabledForRun = run.pushLifecycleRequested;
							const bool silentAssistantReply =
								RuntimeTranscriptGuard::IsSilentReplyText(run.assistantText);
							const std::uint64_t progressAtMs =
								run.lastProgressAtMs > 0
								? run.lastProgressAtMs
								: (run.lastEmitMs > 0 ? run.lastEmitMs : run.startedAtMs);
							const std::uint64_t noProgressElapsedMs =
								nowMs > progressAtMs ? (nowMs - progressAtMs) : 0;
							const bool runHasUnfinishedOutput =
								run.providerDeltaCursor < run.providerDeltas.size() ||
								run.streamCursor < run.assistantText.size();

							if (!run.terminalWaitExceededNotified &&
								runHasUnfinishedOutput &&
								noProgressElapsedMs >= terminalWaitExceededThresholdMs) {
								run.terminalWaitExceededNotified = true;
								if (reconcileFocusRunId.empty() || noProgressElapsedMs > reconcileFocusElapsedMs) {
									reconcileFocusRunId = run.runId;
									reconcileFocusElapsedMs = noProgressElapsedMs;
								}
								++pollMetrics.terminalWaitExceededTotal;
								EmitTelemetryEvent(
									"chat_poll_terminal_wait_exceeded_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(pollMetrics.terminalWaitExceededTotal) + "}");
							}

							if (!run.failed &&
								runHasUnfinishedOutput &&
								noProgressElapsedMs >= stalledActiveRunTimeoutMs) {
								++stalledRunCountThisPoll;
								if (reconcileFocusRunId.empty() || noProgressElapsedMs > reconcileFocusElapsedMs) {
									reconcileFocusRunId = run.runId;
									reconcileFocusElapsedMs = noProgressElapsedMs;
								}
								++pollMetrics.stalledActiveRunTotal;
								run.failed = true;
								run.errorCode = "chat_poll_stalled_active_run";
								run.errorMessage =
									"Run reconciliation stalled without progress; terminalized by poll guard.";
								run.errorContextJson = JsonObject({
									{"layer", JsonString("chat.events.poll")},
									{"guard", JsonString("stalled_active_run_timeout")},
									{"runId", JsonString(run.runId)},
									{"sessionKey", JsonString(run.sessionKey)},
									{"elapsedMs", JsonNumber(noProgressElapsedMs)},
									{"timeoutMs", JsonNumber(stalledActiveRunTimeoutMs)},
									});
								++pollMetrics.stalledActiveRunForcedTerminalTotal;
								EmitTelemetryEvent(
									"chat_poll_stalled_active_run_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(pollMetrics.stalledActiveRunTotal) + "}");
								EmitTelemetryEvent(
									"chat_poll_stalled_active_run_forced_terminal_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(pollMetrics.stalledActiveRunForcedTerminalTotal) + "}");
							}

							const bool enoughTimeElapsed =
								run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;
							if (!run.failed &&
								!silentAssistantReply &&
								runHasUnfinishedOutput &&
								enoughTimeElapsed) {
								ChatControlPlaneService controlPlaneService;
								const auto pollDecision =
									ChatControlPlaneService::SendControlDecision{
										.route = ChatRoutePolicy::Output{
											.originatingChannel = run.originatingChannel,
											.originatingTo = run.originatingTo,
											.explicitDeliverRoute = run.explicitDeliverRoute,
											.reasonCode = "persisted_run_state",
										},
										.toolEvents = ToolEventRecipientPolicy::Output{
											.wantsToolEvents = run.toolEventsAllowed,
											.reasonCode = "persisted_run_state",
										},
								};

								std::string deltaText;
								std::string revealMode = "provider_stream";
								std::size_t revealChunkSize = 0;
								bool hasDeltaToEmit = false;
								if (run.providerDeltaCursor < run.providerDeltas.size()) {
									while (run.providerDeltaCursor < run.providerDeltas.size()) {
										deltaText = run.providerDeltas[run.providerDeltaCursor];
										++run.providerDeltaCursor;
										if (controlPlaneService.ShouldPublishToolDelta(deltaText, pollDecision)) {
											if (deltaText.find("tools.execute") == 0) {
												const auto recipientsIt =
													runTracking.toolEventRecipientsByRun.find(run.runId);
												if (recipientsIt == runTracking.toolEventRecipientsByRun.end() ||
													recipientsIt->second.empty()) {
													deltaText.clear();
													continue;
												}
											}
											hasDeltaToEmit = true;
											break;
										}
										deltaText.clear();
									}
									if (hasDeltaToEmit) {
										run.streamCursor = deltaText.size();
										revealChunkSize = deltaText.size();
									}
								}
								else {
									revealMode = "synthetic_incremental_diff";
									const std::size_t previousCursor = run.streamCursor;
									const std::size_t totalSize = run.assistantText.size();
									const std::size_t remaining =
										totalSize > previousCursor ? (totalSize - previousCursor) : 0;
									std::size_t chunkSize = 8;
									if (fastSyntheticRevealMode) {
										if (totalSize > 6000) {
											chunkSize = 256;
										}
										else if (totalSize > 2000) {
											chunkSize = 128;
										}
										else if (totalSize > 800) {
											chunkSize = 64;
										}
										else {
											chunkSize = 32;
										}
									}
									const bool revealTimedOut =
										nowMs > run.startedAtMs &&
										(nowMs - run.startedAtMs) >= syntheticRevealMaxDurationMs;
									if (revealTimedOut && remaining > 0) {
										chunkSize = remaining;
										revealMode = "direct_full";
									}
									const std::size_t nextCursor =
										(std::min)(totalSize, previousCursor + chunkSize);
									run.streamCursor = nextCursor;
									revealChunkSize = nextCursor > previousCursor
										? (nextCursor - previousCursor)
										: 0;
									deltaText = run.assistantText.substr(
										previousCursor,
										nextCursor > previousCursor ? (nextCursor - previousCursor) : 0);
									hasDeltaToEmit = !deltaText.empty();
								}

								if (hasDeltaToEmit) {
									run.lastEmitMs = nowMs;
									run.lastProgressAtMs = nowMs;
									run.terminalWaitExceededNotified = false;
									const std::string pollDeltaMessage =
										BuildAssistantDeltaMessageJson(deltaText);
									EmitTelemetryEvent(
										"gateway.chat.poll.reveal.mode",
										std::string("{\"runId\":") + JsonString(run.runId) +
										",\"sessionKey\":" + JsonString(run.sessionKey) +
										",\"revealMode\":" + JsonString(revealMode) +
										",\"assistantTextBytes\":" + std::to_string(run.assistantText.size()) +
										",\"streamCursor\":" + std::to_string(run.streamCursor) +
										",\"pollRevealChunkSize\":" + std::to_string(revealChunkSize) + "}");

									PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
										   .runId = run.runId,
										   .sessionKey = run.sessionKey,
										   .state = "delta",
										   .messageJson = pollDeltaMessage,
										   .errorMessage = std::nullopt,
										   .approvalRequired = false,
										   .approvalToken = std::nullopt,
										   .approvalTokenExpiresAtEpochMs = std::nullopt,
										   .approvalNextAction = std::nullopt,
										   .terminalReason = std::nullopt,
										   .timestampMs = nowMs,
										});
									if (pushLifecycleEnabledForRun) {
										EmitPushLifecycleEvent(
											*runtime.transport,
											*runtime.eventFanout,
											GatewayEventFanoutService::ChatLifecycleEvent{
												.runId = run.runId,
												.sessionKey = run.sessionKey,
												.state = "delta",
												.messageJson = pollDeltaMessage,
												.errorMessage = std::nullopt,
												.timestampMs = nowMs,
											},
											sessions.pushEventSeq);
									}
									EmitDeepSeekGatewayDiagnostic(
										"event.enqueue",
										std::string("state=delta runId=") +
										run.runId +
										" session=" +
										run.sessionKey +
										" queueSize=" +
										std::to_string(queue.size()));
								}
							}

							const bool streamCompleted =
								run.failed ||
								silentAssistantReply ||
								(run.providerDeltaCursor >= run.providerDeltas.size() &&
									run.streamCursor >= run.assistantText.size());
							if (streamCompleted && !run.terminalEventEnqueued) {
								const std::optional<std::string> terminalMessage =
									run.failed
									? (run.assistantText.empty() || silentAssistantReply
										? std::nullopt
										: std::optional<std::string>(
											BuildAssistantFinalMessageJson(run.assistantText, nowMs)))
									: (silentAssistantReply
										? std::nullopt
										: std::optional<std::string>(
											BuildAssistantFinalMessageJson(run.assistantText, nowMs)));
								const std::optional<std::string> terminalError =
									run.failed
									? std::optional<std::string>(run.errorMessage.empty()
										? "chat error"
										: run.errorMessage)
									: std::nullopt;
								const std::string runTerminalState = run.failed
									? "error"
									: (run.terminalState.empty() ? "final" : run.terminalState);

								PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
									   .runId = run.runId,
									   .sessionKey = run.sessionKey,
									   .state = runTerminalState,
									   .messageJson = terminalMessage,
									   .errorMessage = terminalError,
									   .approvalRequired = runTerminalState == "needs_approval"
										   ? run.approvalRequired
										   : false,
									   .approvalToken = (runTerminalState == "needs_approval" &&
										   !run.approvalToken.empty())
										   ? std::optional<std::string>(run.approvalToken)
										   : std::nullopt,
									   .approvalTokenExpiresAtEpochMs =
										   (runTerminalState == "needs_approval" &&
											run.approvalTokenExpiresAtEpochMs > 0)
										   ? std::optional<std::uint64_t>(run.approvalTokenExpiresAtEpochMs)
										   : std::nullopt,
									   .approvalNextAction = (runTerminalState == "needs_approval" &&
										   !run.approvalNextAction.empty())
										   ? std::optional<std::string>(run.approvalNextAction)
										   : std::nullopt,
									   .terminalReason = run.terminalReason.empty()
										   ? std::nullopt
										   : std::optional<std::string>(run.terminalReason),
									   .timestampMs = nowMs,
									});
								if (pushLifecycleEnabledForRun) {
									EmitPushLifecycleEvent(
										*runtime.transport,
										*runtime.eventFanout,
										GatewayEventFanoutService::ChatLifecycleEvent{
											.runId = run.runId,
											.sessionKey = run.sessionKey,
											.state = runTerminalState,
											.messageJson = terminalMessage,
											.errorMessage = terminalError,
											.timestampMs = nowMs,
										},
										sessions.pushEventSeq);
								}
								GatewayLifecycleEventEmitter::EmitLifecycle(
									runTerminalState,
									run.runId,
									run.sessionKey,
									nowMs,
									terminalError);
								run.terminalEventEnqueued = true;
								run.lastProgressAtMs = nowMs;
								runtime.transportRecipientRegistry->MarkRunFinalized(
									run.runId,
									nowMs);
								EmitDeepSeekGatewayDiagnostic(
									"event.enqueue",
									std::string("state=") +
									runTerminalState +
									" runId=" +
									run.runId +
									" session=" +
									run.sessionKey +
									" queueSize=" +
									std::to_string(queue.size()));

								run.active = false;
								runTracking.toolEventRecipientsByRun.erase(run.runId);
								runtime.transportRecipientRegistry->PruneExpired(nowMs);
							}
							};

						for (std::size_t runIndex = 0; runIndex < reconcileCount; ++runIndex) {
							if (sessionActiveRuns[runIndex] != nullptr) {
								processRun(*sessionActiveRuns[runIndex]);
							}
						}

						std::string eventsJson = "[";
						std::size_t emitted = 0;
						std::unordered_set<std::string> terminalRunIdsSeenThisPoll;
						if (!queue.empty()) {
							while (emitted < limit && !queue.empty()) {
								const GatewayHost::ChatEventState eventState = queue.front();
								queue.pop_front();

								if (IsTerminalChatState(eventState.state)) {
									if (run.terminalDeliveredRunIds.find(eventState.runId) != run.terminalDeliveredRunIds.end() ||
										terminalRunIdsSeenThisPoll.find(eventState.runId) != terminalRunIdsSeenThisPoll.end()) {
										continue;
									}

									terminalRunIdsSeenThisPoll.insert(eventState.runId);
									run.terminalDeliveredRunIds.insert(eventState.runId);
									if (run.terminalDeliveredRunIds.size() > 1024) {
										run.terminalDeliveredRunIds.clear();
									}
								}

								EmitDeepSeekGatewayDiagnostic(
									"event.dequeue",
									std::string("state=") +
									eventState.state +
									" runId=" +
									eventState.runId +
									" session=" +
									eventState.sessionKey +
									" emitted=" +
									std::to_string(emitted + 1) +
									" queueRemaining=" +
									std::to_string(queue.size()));

								if (emitted > 0) {
									eventsJson += ",";
								}

								std::optional<std::string> eventErrorCode;
								std::optional<std::string> eventContextJson;
								if (eventState.state == "error") {
									const auto runContextIt = run.runsById.find(eventState.runId);
									if (runContextIt != run.runsById.end()) {
										if (!runContextIt->second.errorCode.empty()) {
											eventErrorCode = runContextIt->second.errorCode;
										}
										if (!runContextIt->second.errorContextJson.empty()) {
											eventContextJson = runContextIt->second.errorContextJson;
										}
									}
								}
								eventsJson += BuildChatEventJson(
									eventState.runId,
									eventState.sessionKey,
									eventState.state,
									eventState.messageJson,
									eventErrorCode,
									eventState.errorMessage,
									eventContextJson,
									eventState.approvalRequired,
									eventState.approvalToken,
									eventState.approvalTokenExpiresAtEpochMs,
									eventState.approvalNextAction,
									eventState.terminalReason,
									eventState.timestampMs);
								++emitted;

								if ((eventState.state == "final" ||
									eventState.state == "aborted") &&
									eventState.messageJson.has_value() &&
									!IsSilentAssistantMessageJson(eventState.messageJson.value()))
								{
									bool isDetachedRun = false;
									const auto runContextIt = run.runsById.find(eventState.runId);
									if (runContextIt != run.runsById.end()) {
										isDetachedRun = runContextIt->second.detached;
									}

									if (!isDetachedRun) {
										PushHistoryMessageIfNew(
											sessions.historyBySession[sessionKey],
											eventState.messageJson.value());
									}
								}

								if (IsTerminalChatState(eventState.state)) {
									const auto runIt = run.runsById.find(eventState.runId);
									if (runIt != run.runsById.end()) {
										if (!runIt->second.idempotencyKey.empty()) {
											run.runByIdempotency.erase(
												runIt->second.idempotencyKey);
										}

										run.runsById.erase(runIt);
										runtime.transportRecipientRegistry->PruneRun(eventState.runId);
									}
								}
							}
						}

						eventsJson += "]";
						const bool waitingForTerminalEvent = emitted == 0 && !sessionActiveRuns.empty();
						std::string reconcileState = "idle";
						if (waitingForTerminalEvent) {
							reconcileState = stalledRunCountThisPoll > 0
								? "reconciling_stalled_run"
								: "waiting_for_terminal_event";
						}
						const std::string statusMessage = waitingForTerminalEvent
							? (reconcileFocusRunId.empty()
								? std::string("run is being reconciled")
								: (std::string("run is being reconciled (runId=") +
									reconcileFocusRunId +
									", elapsedMs=" +
									std::to_string(reconcileFocusElapsedMs) +
									")"))
							: std::string();
						const std::string reconcileJson = JsonObject({
							{"state", JsonString(reconcileState)},
							{"waitingForTerminalEvent", JsonBool(waitingForTerminalEvent)},
							{"activeRuns", JsonNumber(static_cast<std::uint64_t>(sessionActiveRuns.size()))},
							{"reconciledRuns", JsonNumber(static_cast<std::uint64_t>(reconcileCount))},
							{"stalledRuns", JsonNumber(static_cast<std::uint64_t>(stalledRunCountThisPoll))},
							{"focusRunId", JsonString(reconcileFocusRunId)},
							{"focusElapsedMs", JsonNumber(reconcileFocusElapsedMs)},
							{"message", JsonString(statusMessage)},
							});
						EmitDeepSeekGatewayDiagnostic(
							"event.dequeue",
							std::string("poll complete session=") +
							sessionKey +
							" emitted=" +
							std::to_string(emitted) +
							" queueRemaining=" +
							std::to_string(queue.size()) +
							" activeRuns=" +
							std::to_string(sessionActiveRuns.size()) +
							" stalledRuns=" +
							std::to_string(stalledRunCountThisPoll));
						return protocol::OkResponse(request, "{\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\",\"events\":" +
							eventsJson +
							",\"count\":" +
							std::to_string(emitted) +
							",\"reconcile\":" +
							reconcileJson +
							"}");
				});

			dispatcher->Register(
				"gateway.skills.status",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;

					return protocol::OkResponse(request, "{\"total\":" +
						std::to_string(state.entries.size()) +
						",\"rootsScanned\":" +
						std::to_string(state.rootsScanned) +
						",\"rootsSkipped\":" +
						std::to_string(state.rootsSkipped) +
						",\"pluginRootsConfigured\":" +
						std::to_string(state.pluginRootsConfigured) +
						",\"pluginRootsScanned\":" +
						std::to_string(state.pluginRootsScanned) +
						",\"loaderPolicyRejectPathSymlink\":" +
						std::to_string(state.loaderPolicyRejectPathSymlinkCount) +
						",\"loaderPolicyStrictFrontmatter\":" +
						std::to_string(state.loaderPolicyStrictFrontmatterCount) +
						",\"symlinkRejectedFiles\":" +
						std::to_string(state.symlinkRejectedFiles) +
						",\"strictFrontmatterOmittedFiles\":" +
						std::to_string(state.strictFrontmatterOmittedFiles) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
						",\"oversizedSkillFiles\":" +
						std::to_string(state.oversizedSkillFiles) +
						",\"invalidFrontmatterFiles\":" +
						std::to_string(state.invalidFrontmatterFiles) +
						",\"eligible\":" +
						std::to_string(state.eligibleCount) +
						",\"disabled\":" +
						std::to_string(state.disabledCount) +
						",\"blockedByAllowlist\":" +
						std::to_string(state.blockedByAllowlistCount) +
						",\"missingRequirements\":" +
						std::to_string(state.missingRequirementsCount) +
						",\"strictEntryResolutionMode\":" +
						std::to_string(state.strictEntryResolutionModeCount) +
						",\"compatEntryResolutionMode\":" +
						std::to_string(state.compatEntryResolutionModeCount) +
						",\"configResolvedByKey\":" +
						std::to_string(state.configResolvedByKeyCount) +
						",\"configResolvedByNameFallback\":" +
						std::to_string(state.configResolvedByNameFallbackCount) +
						",\"allowlistRaw\":" +
						std::to_string(state.allowlistRawCount) +
						",\"allowlistNormalized\":" +
						std::to_string(state.allowlistNormalizedCount) +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"remoteEligibilityEnabled\":" +
						std::to_string(state.remoteEligibilityEnabledCount) +
						",\"remotePlatformSatisfied\":" +
						std::to_string(state.remotePlatformSatisfiedCount) +
						",\"remoteBinSatisfied\":" +
						std::to_string(state.remoteBinSatisfiedCount) +
						",\"remoteAnyBinSatisfied\":" +
						std::to_string(state.remoteAnyBinSatisfiedCount) +
						",\"alwaysBypass\":" +
						std::to_string(state.alwaysBypassCount) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"dispatchRequiredSkills\":" +
						std::to_string(state.dispatchRequiredSkillCount) +
						",\"dispatchRequiredMissing\":" +
						std::to_string(state.dispatchRequiredMissingCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
						",\"promptIncluded\":" +
						std::to_string(state.promptIncludedCount) +
						",\"promptChars\":" +
						std::to_string(state.promptChars) +
						",\"promptTruncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						",\"snapshotVersion\":" +
						std::to_string(state.snapshotVersion) +
						",\"watchEnabled\":" +
						std::string(state.watchEnabled ? "true" : "false") +
						",\"watchDebounceMs\":" +
						std::to_string(state.watchDebounceMs) +
						",\"watchReason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"" +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"sandboxDestinationNamingMode\":\"" +
						EscapeJsonLocal(state.sandboxDestinationNamingMode) +
						"\"" +
						",\"sandboxDestinationCollisions\":" +
						std::to_string(state.sandboxDestinationCollisions) +
						",\"sandboxSourceDirFallbacks\":" +
						std::to_string(state.sandboxSourceDirFallbacks) +
						",\"sandboxSynced\":" +
						std::to_string(state.sandboxSynced) +
						",\"sandboxSkipped\":" +
						std::to_string(state.sandboxSkipped) +
						",\"envAllowed\":" +
						std::to_string(state.envAllowed) +
						",\"envBlocked\":" +
						std::to_string(state.envBlocked) +
						",\"installExecutable\":" +
						std::to_string(state.installExecutableCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"installContractProjected\":" +
						std::to_string(state.installContractProjectedCount) +
						",\"installContractFallback\":" +
						std::to_string(state.installContractFallbackCount) +
						",\"scanInfo\":" +
						std::to_string(state.scanInfoCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanFiles\":" +
						std::to_string(state.scanScannedFiles) +
						",\"warnings\":" +
						std::to_string(state.warningCount) +
						"}");
				});

			dispatcher->Register(
				"skills.status",
				[dispatcher](const protocol::RequestFrame& request) {
					protocol::RequestFrame delegated = request;
					delegated.method = "gateway.skills.status";
					return dispatcher->Dispatch(delegated);
				});

			dispatcher->Register(
				"gateway.skills.install.options",
				[skills](const protocol::RequestFrame& request) {
					std::string optionsJson = "[";
					bool first = true;
					std::size_t count = 0;
					for (const auto& entry : skills.catalogState.entries) {
						if (entry.installKind.empty()) {
							continue;
						}

						if (!first) {
							optionsJson += ",";
						}

						optionsJson +=
							"{\"skill\":\"" +
							EscapeJsonLocal(entry.name) +
							"\",\"kind\":\"" +
							EscapeJsonLocal(entry.installKind) +
							"\",\"command\":\"" +
							EscapeJsonLocal(entry.installCommand) +
							"\",\"executable\":" +
							std::string(entry.installExecutable ? "true" : "false") +
							",\"reason\":\"" +
							EscapeJsonLocal(entry.installReason) +
							"\"}";
						first = false;
						++count;
					}

					optionsJson += "]";
					return protocol::OkResponse(request, "{\"options\":" +
						optionsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.install.execute",
				[skills](const protocol::RequestFrame& request) {
					const auto skillName =
						ExtractStringParam(request.paramsJson, "skill");
					const auto it = std::find_if(
						skills.catalogState.entries.begin(),
						skills.catalogState.entries.end(),
						[&skillName](const SkillsCatalogGatewayEntry& item) {
							return item.name == skillName;
						});

					if (it == skills.catalogState.entries.end()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "skill_not_found",
										.message = "Requested skill install target was not found.",
										.detailsJson = "{\"skill\":\"" +
											EscapeJsonLocal(skillName) +
											"\"}",
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto warning = skills.catalogState.scanCriticalCount > 0
						? "security_scan_critical"
						: (skills.catalogState.scanWarnCount > 0
							? "security_scan_warn"
							: "none");

					return protocol::OkResponse(request, "{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"kind\":\"" +
						EscapeJsonLocal(it->installKind) +
						"\",\"command\":\"" +
						EscapeJsonLocal(it->installCommand) +
						"\",\"executed\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"warning\":\"" +
						warning +
						"\",\"scanCritical\":" +
						std::to_string(skills.catalogState.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(skills.catalogState.scanWarnCount) +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.scan.status",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;
					return protocol::OkResponse(request, "{\"files\":" +
						std::to_string(state.scanScannedFiles) +
						",\"info\":" +
						std::to_string(state.scanInfoCount) +
						",\"warn\":" +
						std::to_string(state.scanWarnCount) +
						",\"critical\":" +
						std::to_string(state.scanCriticalCount) +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.sandbox.status",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;
					return protocol::OkResponse(request, "{\"ok\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"synced\":" +
						std::to_string(state.sandboxSynced) +
						",\"skipped\":" +
						std::to_string(state.sandboxSkipped) +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.env.status",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;
					return protocol::OkResponse(request, "{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}");
				});

			dispatcher->Register(
				"gateway.config.schema.get",
				[configSchema](const protocol::RequestFrame& request) {
					if (!configSchema.getCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "not_supported",
										.message =
											"Config schema callback is not configured.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto state = configSchema.getCallback();
					const std::string payload =
						"{\"schema\":" +
						NormalizeJsonRawForPayload(state.schemaJson, "{}") +
						",\"uiHints\":" +
						NormalizeJsonRawForPayload(state.uiHintsJson, "{}") +
						",\"version\":\"" +
						EscapeJsonLocal(state.version) +
						"\",\"generatedAt\":\"" +
						EscapeJsonLocal(state.generatedAt) +
						"\"}";

					return protocol::OkResponse(request, payload);
				});

			dispatcher->Register(
				"gateway.config.schema.lookup",
				[configSchema](const protocol::RequestFrame& request) {
					if (!configSchema.lookupCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "not_supported",
										.message =
											"Config schema lookup callback is not configured.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const std::string requestedPath =
						ExtractStringParam(request.paramsJson, "path");
					const auto normalizedPath =
						NormalizeSchemaLookupPathRequest(requestedPath);
					if (!normalizedPath.has_value()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_lookup_path",
										.message =
											"Invalid schema lookup path.",
										.detailsJson =
											"{\"path\":\"" +
											EscapeJsonLocal(requestedPath) +
											"\"}",
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto lookupResult =
						configSchema.lookupCallback(normalizedPath.value());
					if (!lookupResult.has_value()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "schema_path_not_found",
										.message = "Schema path was not found.",
										.detailsJson =
											"{\"path\":\"" +
											EscapeJsonLocal(normalizedPath.value()) +
											"\"}",
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, SerializeConfigSchemaLookupResultLocal(
						lookupResult.value()));
				});

			dispatcher->Register(
				"gateway.skills.info",
				[skills](const protocol::RequestFrame& request) {
					const auto skillName =
						ExtractStringParam(request.paramsJson, "skill");
					if (skillName.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "missing_skill",
										.message = "Parameter `skill` is required.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto it = std::find_if(
						skills.catalogState.entries.begin(),
						skills.catalogState.entries.end(),
						[&skillName](const SkillsCatalogGatewayEntry& entry) {
							return entry.name == skillName ||
								entry.skillKey == skillName;
						});

					if (it == skills.catalogState.entries.end()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "skill_not_found",
										.message = "Skill not found.",
										.detailsJson =
											"{\"skill\":\"" +
											EscapeJsonLocal(skillName) +
											"\"}",
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, "{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"skillKey\":\"" +
						EscapeJsonLocal(it->skillKey) +
						"\",\"primaryEnv\":\"" +
						EscapeJsonLocal(it->primaryEnv) +
						"\",\"requiresEnv\":" +
						SerializeStringArrayLocal(it->requiresEnv) +
						",\"requiresConfig\":" +
						SerializeStringArrayLocal(it->requiresConfig) +
						",\"missingEnv\":" +
						SerializeStringArrayLocal(it->missingEnv) +
						",\"missingConfig\":" +
						SerializeStringArrayLocal(it->missingConfig) +
						",\"missingBins\":" +
						SerializeStringArrayLocal(it->missingBins) +
						",\"missingAnyBins\":" +
						SerializeStringArrayLocal(it->missingAnyBins) +
						",\"eligible\":" +
						std::string(it->eligible ? "true" : "false") +
						",\"disabled\":" +
						std::string(it->disabled ? "true" : "false") +
						",\"blockedByAllowlist\":" +
						std::string(it->blockedByAllowlist ? "true" : "false") +
						",\"installKind\":\"" +
						EscapeJsonLocal(it->installKind) +
						"\",\"installReason\":\"" +
						EscapeJsonLocal(it->installReason) +
						"\",\"installExecutable\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"scanCritical\":" +
						std::to_string(skills.catalogState.scanCriticalCount) +
						"}");
				});

			// Skills update: no param parsing here; forward to skills.updateCallback (BlazeClaw:
			// SkillsGatewayMethodHandler::HandleSkillsUpdate). Alias "skills.update" below rewrites method only.
			dispatcher->Register(
				"gateway.skills.update",
				[skills](const protocol::RequestFrame& request) {
					if (!skills.updateCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "not_supported",
										.message = "Skills update callback is not configured.",
										.detailsJson = std::nullopt,
									  .retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return skills.updateCallback(request);
				});

			dispatcher->Register(
				"skills.update",
				[skills](const protocol::RequestFrame& request) {
					protocol::RequestFrame delegated = request;
					delegated.method = "gateway.skills.update";
					if (!skills.updateCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "not_supported",
										.message = "Skills update callback is not configured.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return skills.updateCallback(delegated);
				});

			dispatcher->Register(
				"gateway.skills.check",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;
					const bool ok =
						state.scanCriticalCount == 0 &&
						state.installBlockedCount == 0 &&
						state.sandboxSyncOk &&
						state.dispatchRequiredMissingCount == 0;

					return protocol::OkResponse(request, "{\"ok\":" +
						std::string(ok ? "true" : "false") +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"dispatchRequiredMissing\":" +
						std::to_string(state.dispatchRequiredMissingCount) +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.diagnostics",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;
					std::vector<std::string> hints;
					if (state.installBlockedCount > 0) {
						hints.push_back("skills.install.options");
					}

					if (state.scanCriticalCount > 0) {
						hints.push_back("skills.scan.status");
					}

					if (state.verifiedOpenPathFailures > 0 ||
						state.verifiedOpenValidationFailures > 0 ||
						state.verifiedOpenIoFailures > 0) {
						hints.push_back("skills.local-loader.verified-open");
					}

					if (state.commandMissingToolDispatchCount > 0 ||
						state.commandSkillNameDedupeCount > 0 ||
						state.commandInvalidArgModeFallbackCount > 0 ||
						state.commandSourceContributionCount > 0) {
						hints.push_back("skills.command-specs");
					}
					if (state.dispatchRequiredMissingCount > 0) {
						hints.push_back("skills.dispatch.contract");
					}

					if (state.bundleCommandRootsScannedCount > 0 ||
						state.bundleCommandFilesLoadedCount > 0 ||
						state.bundleCommandFilesSkippedDisabledCount > 0 ||
						state.bundleCommandFilesSkippedEmptyPromptCount > 0 ||
						state.bundleCommandFilesSkippedInvalidNameCount > 0 ||
						state.bundleCommandFilesRejectedUnsafeCount > 0) {
						hints.push_back("plugins.bundle-commands");
					}

					if (state.entryConfigRawCount > 0 ||
						state.entryConfigNormalizedCount > 0 ||
						state.entryConfigMalformedCount > 0) {
						hints.push_back("skills.config.entries");
					}

					if (!state.sandboxSyncOk) {
						hints.push_back("skills.sandbox.status");
					}

					if (state.executionReadinessMismatchCount > 0) {
						hints.push_back("skills.execution.readiness");
					}

					std::string hintsJson = "[";
					for (std::size_t index = 0; index < hints.size(); ++index) {
						if (index > 0) {
							hintsJson += ",";
						}

						hintsJson +=
							"\"" +
							EscapeJsonLocal(hints[index]) +
							"\"";
					}

					hintsJson += "]";
					std::string mismatchSampleJson = "[";
					for (std::size_t index = 0;
						index < state.executionReadinessMismatchSample.size();
						++index) {
						if (index > 0) {
							mismatchSampleJson += ",";
						}
						mismatchSampleJson +=
							"\"" +
							EscapeJsonLocal(state.executionReadinessMismatchSample[index]) +
							"\"";
					}
					mismatchSampleJson += "]";

					std::string effectiveRootsJson = "[";
					for (std::size_t index = 0;
						index < state.effectiveSkillRoots.size();
						++index) {
						if (index > 0) {
							effectiveRootsJson += ",";
						}
						effectiveRootsJson +=
							"\"" +
							EscapeJsonLocal(state.effectiveSkillRoots[index]) +
							"\"";
					}
					effectiveRootsJson += "]";

					return protocol::OkResponse(request, "{\"warnings\":" +
						std::to_string(state.warningCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"dispatchRequiredSkills\":" +
						std::to_string(state.dispatchRequiredSkillCount) +
						",\"dispatchRequiredMissing\":" +
						std::to_string(state.dispatchRequiredMissingCount) +
						",\"projectedToolDispatchCount\":" +
						std::to_string(state.projectedToolDispatchCount) +
						",\"runtimeRegisteredSkillToolCount\":" +
						std::to_string(state.runtimeRegisteredSkillToolCount) +
						",\"executionReadinessMismatchCount\":" +
						std::to_string(state.executionReadinessMismatchCount) +
						",\"executionReadinessMismatchSample\":" +
						mismatchSampleJson +
						",\"effectiveSkillRootCount\":" +
						std::to_string(state.effectiveSkillRootCount) +
						",\"effectiveSkillRoots\":" +
						effectiveRootsJson +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
						",\"hints\":" +
						hintsJson +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.prompt",
				[skills](const protocol::RequestFrame& request) {
					const auto& state = skills.catalogState;

					return protocol::OkResponse(request, "{\"prompt\":\"" +
						EscapeJsonLocal(state.prompt) +
						"\",\"included\":" +
						std::to_string(state.promptIncludedCount) +
						",\"chars\":" +
						std::to_string(state.promptChars) +
						",\"truncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						"}");
				});

			dispatcher->Register(
				"gateway.skills.commands",
				[skills](const protocol::RequestFrame& request) {
					std::string commandsJson = "[";
					bool first = true;
					std::size_t count = 0;

					for (const auto& entry : skills.catalogState.entries) {
						if (entry.commandName.empty()) {
							continue;
						}

						if (!first) {
							commandsJson += ",";
						}

						commandsJson +=
							"{\"name\":\"" +
							EscapeJsonLocal(entry.commandName) +
							"\",\"skill\":\"" +
							EscapeJsonLocal(entry.name) +
							"\",\"toolName\":\"" +
							EscapeJsonLocal(entry.commandToolName) +
							"\",\"argMode\":\"" +
							EscapeJsonLocal(entry.commandArgMode) +
							"\",\"argSchema\":\"" +
							EscapeJsonLocal(entry.commandArgSchema) +
							"\",\"resultSchema\":\"" +
							EscapeJsonLocal(entry.commandResultSchema) +
							"\",\"idempotencyHint\":\"" +
							EscapeJsonLocal(entry.commandIdempotencyHint) +
							"\",\"retryPolicyHint\":\"" +
							EscapeJsonLocal(entry.commandRetryPolicyHint) +
							"\",\"requiresApproval\":" +
							std::string(entry.commandRequiresApproval ? "true" : "false") +
							"\",\"description\":\"" +
							EscapeJsonLocal(entry.description) +
							"\"}";
						first = false;
						++count;
					}

					commandsJson += "]";
					return protocol::OkResponse(request, "{\"commands\":" +
						commandsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
				});

			dispatcher->Register(
				"skills.commands",
				[dispatcher](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "gateway.skills.commands";
					return dispatcher->Dispatch(forwarded);
				});

			dispatcher->Register(
				"gateway.skills.refresh",
				[skills](const protocol::RequestFrame& request) {
					bool refreshed = false;
					if (skills.refreshCallback) {
						skills.catalogState = skills.refreshCallback();
						refreshed = true;
					}

					const auto& state = skills.catalogState;
					return protocol::OkResponse(request, "{\"refreshed\":" +
						std::string(refreshed ? "true" : "false") +
						",\"version\":" +
						std::to_string(state.snapshotVersion) +
						",\"reason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"}");
				});

			dispatcher->Register(
				"gateway.skills.list",
				[skills](const protocol::RequestFrame& request) {
					const auto includeInvalid =
						ExtractBoolParam(request.paramsJson, "includeInvalid");
					const bool shouldIncludeInvalid =
						!includeInvalid.has_value() || includeInvalid.value();

					std::string entriesJson = "[";
					bool first = true;
					std::size_t count = 0;
					for (const auto& entry : skills.catalogState.entries) {
						if (!shouldIncludeInvalid && !entry.validFrontmatter) {
							continue;
						}

						if (!first) {
							entriesJson += ",";
						}

						entriesJson += SerializeSkillCatalogEntry(entry);
						first = false;
						++count;
					}
					entriesJson += "]";

					return protocol::OkResponse(request, "{\"skills\":" +
						entriesJson +
						",\"count\":" +
						std::to_string(count) +
						",\"includeInvalid\":" +
						std::string(shouldIncludeInvalid ? "true" : "false") +
						"}");
				});
		}


	} // namespace handlers::runtime

} // namespace blazeclaw::gateway
