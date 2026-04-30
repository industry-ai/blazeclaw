#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayHostRuntimeLocalHelpers.h"
#include "GatewayHostProtocolHelpers.h"
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

			host.RuntimeContext().dispatcher->Register(
				"agent",
				[&host](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					return host.RuntimeContext().dispatcher->Dispatch(forwarded);
				});
			host.RuntimeContext().dispatcher->Register(
				"send",
				[&host](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					return host.RuntimeContext().dispatcher->Dispatch(forwarded);
				});
			host.RuntimeContext().dispatcher->Register(
				"wake",
				[](const protocol::RequestFrame& request) {
					return protocol::OkResponse(
						request,
						"{\"accepted\":true,\"wake\":true}");
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
			auto resolveSessionId = [&host](const std::optional<std::string>& paramsJson) {
				const RequestParamsView params(paramsJson);
				const std::string requestedSessionId = params.GetString("sessionId");
				return host.RuntimeContext().sessionRegistry->Resolve(requestedSessionId).id;
				};
			auto resolveConnectionId = [](const std::optional<std::string>& paramsJson) {
				const RequestParamsView params(paramsJson);
				std::string connectionId = params.GetString("connectionId");
				if (connectionId.empty()) {
					connectionId = params.GetString("clientConnectionId");
				}
				if (connectionId.empty()) {
					connectionId = "local";
				}
				return connectionId;
				};

			host.RuntimeContext().dispatcher->Register(
				"sessions.subscribe",
				[sessionState, resolveSessionId, resolveConnectionId](const protocol::RequestFrame& request) {
					const std::string sessionId = resolveSessionId(request.paramsJson);
					const std::string connectionId = resolveConnectionId(request.paramsJson);
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
			host.RuntimeContext().dispatcher->Register(
				"sessions.unsubscribe",
				[sessionState, resolveSessionId, resolveConnectionId](const protocol::RequestFrame& request) {
					const std::string sessionId = resolveSessionId(request.paramsJson);
					const std::string connectionId = resolveConnectionId(request.paramsJson);
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

			host.RuntimeContext().dispatcher->Register(
				"sessions.messages.subscribe",
				[sessionState, resolveSessionId, resolveConnectionId](const protocol::RequestFrame& request) {
					const std::string sessionId = resolveSessionId(request.paramsJson);
					const std::string connectionId = resolveConnectionId(request.paramsJson);
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
			host.RuntimeContext().dispatcher->Register(
				"sessions.messages.unsubscribe",
				[sessionState, resolveSessionId, resolveConnectionId](const protocol::RequestFrame& request) {
					const std::string sessionId = resolveSessionId(request.paramsJson);
					const std::string connectionId = resolveConnectionId(request.paramsJson);
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

			host.RuntimeContext().dispatcher->Register(
				"sessions.send",
				[&host](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.send";
					const protocol::ResponseFrame response = host.RuntimeContext().dispatcher->Dispatch(forwarded);
					if (!response.ok) {
						return response;
					}
					const std::string sessionId = host.RuntimeContext().sessionRegistry->Resolve(RequestParamsView(request.paramsJson).GetString("sessionId")).id;
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
			host.RuntimeContext().dispatcher->Register(
				"sessions.steer",
				[&host](const protocol::RequestFrame& request) {
					auto abortForwarded = request;
					abortForwarded.method = "chat.abort";
					const protocol::ResponseFrame abortResponse = host.RuntimeContext().dispatcher->Dispatch(abortForwarded);
					if (!abortResponse.ok) {
						return abortResponse;
					}

					auto sendForwarded = request;
					sendForwarded.method = "chat.send";
					const protocol::ResponseFrame sendResponse = host.RuntimeContext().dispatcher->Dispatch(sendForwarded);
					if (!sendResponse.ok) {
						return sendResponse;
					}

					const bool interruptedActiveRun =
						abortResponse.payloadJson.has_value() &&
						abortResponse.payloadJson.value().find("\"aborted\":true") != std::string::npos;
					const std::string sessionId =
						host.RuntimeContext().sessionRegistry->Resolve(RequestParamsView(request.paramsJson).GetString("sessionId")).id;
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
			host.RuntimeContext().dispatcher->Register(
				"sessions.abort",
				[&host](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "chat.abort";
					const protocol::ResponseFrame response = host.RuntimeContext().dispatcher->Dispatch(forwarded);
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

			host.RuntimeContext().dispatcher->Register(
				"sessions.compaction.list",
				[sessionState, resolveSessionId](const protocol::RequestFrame& request) {
					const std::string sessionId = resolveSessionId(request.paramsJson);
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
			host.RuntimeContext().dispatcher->Register(
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
			host.RuntimeContext().dispatcher->Register(
				"sessions.compaction.branch",
				[sessionState, resolveSessionId](const protocol::RequestFrame& request) {
					const RequestParamsView params(request.paramsJson);
					const std::string sessionId = resolveSessionId(request.paramsJson);
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
			host.RuntimeContext().dispatcher->Register(
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

			host.RuntimeContext().dispatcher->Register(
				"chat.send",
				[&host](const protocol::RequestFrame& request) {
					ChatRunStageContext stageContext{
						   .requestId = request.id,
						   .method = request.method,
					 .paramsJson = request.paramsJson,
						.validateAttachments = [&host](
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
						.findRunByIdempotency = [&host](const std::string& key)
							-> std::optional<std::string> {
							if (key.empty()) {
								return std::nullopt;
							}

							const auto dedupeIt = host.m_chatRunByIdempotency.find(key);
							if (dedupeIt == host.m_chatRunByIdempotency.end()) {
								return std::nullopt;
							}

							return dedupeIt->second;
						},
					  .extractAttachmentMimeTypes = [&host](
							const std::optional<std::string>& paramsJson) {
							return ExtractAttachmentMimeTypes(paramsJson);
						},
					};
					auto pipelineResult = host.RuntimeContext().chatRunPipeline->Run(stageContext);
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
								host.m_chatReplayByIdempotency.find(stageContext.idempotencyKey);
							if (replayIt != host.m_chatReplayByIdempotency.end()) {
								return protocol::ReplayFromStored(
									request,
									replayIt->second.ok,
									replayIt->second.payloadJson,
									replayIt->second.error);
							}

							return protocol::OkResponse(request, "{\"runId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"queued\":false,\"deduped\":true}");
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
					const std::uint64_t nowMs = stageContext.nowEpochMs > 0
						? stageContext.nowEpochMs
						: CurrentEpochMsLocal();
					const std::string runId = !stageContext.runId.empty()
						? stageContext.runId
						: (!request.id.empty()
							? request.id
							: ("chat-run-" + std::to_string(nowMs) +
								"-" + std::to_string(host.m_chatRunsById.size() + 1)));
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
								host.m_chatHistoryBySession[sessionKey],
								BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));
						}
						userTurnPersisted = true;
						};

					persistUserTurnIfNeeded();
					const bool runAlreadyTracked =
						host.m_chatRunsById.find(runId) != host.m_chatRunsById.end();
					const bool lateJoinRequested =
						runAlreadyTracked &&
						stageContext.hasConnectedClient &&
						!clientConnectionId.empty();
					const bool pushLifecycleEnabled =
						stageContext.pushLifecycleRequested;

					ChatControlPlaneService controlPlaneService;
					const bool hasRegisteredRecipient =
						!clientConnectionId.empty() &&
						host.RuntimeContext().transportRecipientRegistry->HasRecipients(runId);
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
						host.RuntimeContext().transportRecipientRegistry->RegisterRecipient(
							runId,
							sessionKey,
							clientConnectionId,
							nowMs);
						host.RuntimeContext().transportRecipientRegistry->RegisterLateJoin(
							sessionKey,
							clientConnectionId,
							nowMs);
						host.m_chatToolEventRecipientsByRun[runId].insert(clientConnectionId);
						for (const auto& [activeRunId, activeRun] : host.m_chatRunsById) {
							if (activeRunId != runId &&
								activeRun.sessionKey == sessionKey &&
								activeRun.active) {
								host.m_chatToolEventRecipientsByRun[activeRunId].insert(clientConnectionId);
							}
						}
						host.RuntimeContext().transportRecipientRegistry->PruneExpired(nowMs);
					}

					if (lateJoinRequested && !clientConnectionId.empty()) {
						auto& replayQueue = host.m_chatEventsBySession[sessionKey];
						const auto activeRuns =
							host.RuntimeContext().transportRecipientRegistry->ActiveRunsForSession(sessionKey);
						for (const auto& activeRunId : activeRuns) {
							const auto activeRunIt = host.m_chatRunsById.find(activeRunId);
							if (activeRunIt == host.m_chatRunsById.end()) {
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
						"}}"
					);

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
						[&host, &runId, &sessionKey](
							const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
							const bool success) {
								if (taskDeltas.empty()) {
									return;
								}

								std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> normalizedTaskDeltas;
								normalizedTaskDeltas.reserve(taskDeltas.size());
								for (std::size_t index = 0; index < taskDeltas.size(); ++index) {
									normalizedTaskDeltas.push_back(
										TaskDeltaLegacyAdapter::AdaptEntry(
											taskDeltas[index],
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
									host.m_taskDeltaRepository.Upsert(runId, normalizedTaskDeltas);
								(void)upserted;
								const std::size_t evictedFromChat =
									host.m_taskDeltaRepository.EnforceRetentionLimit(
										host.m_taskDeltasRetentionLimit);
								if (evictedFromChat > 0) {
									EmitTelemetryEvent(
										"gateway.taskdelta.retention.evicted",
										std::string("{\"evictedRuns\":") +
										std::to_string(evictedFromChat) +
										",\"remainingRuns\":" +
										std::to_string(host.m_taskDeltaRepository.Size()) +
										",\"reason\":\"chat_runtime_upsert\",\"runId\":" +
										JsonString(runId) + "}");
								}
								host.PersistTaskDeltas();
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
									++host.m_taskDeltaRunSuccessCount;
								}
								else {
									++host.m_taskDeltaRunFailureCount;
								}

								if (terminalErrorCode == "embedded_deadline_exceeded") {
									++host.m_taskDeltaRunTimeoutCount;
								}

								if (terminalErrorCode == "embedded_run_cancelled" ||
									terminalStatus == "skipped") {
									++host.m_taskDeltaRunCancelledCount;
								}

								if (terminalErrorCode.find("fallback") != std::string::npos ||
									terminalStatus == "fallback") {
									++host.m_taskDeltaRunFallbackCount;
								}

								EmitTelemetryEvent(
									"gateway.taskdelta.runSummary",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"count\":" + std::to_string(normalizedTaskDeltas.size()) +
									",\"success\":" + (success ? std::string("true") : std::string("false")) +
									",\"terminalStatus\":" + JsonString(terminalStatus) +
									",\"errorCode\":" + JsonString(terminalErrorCode) +
									",\"totals\":{\"success\":" + std::to_string(host.m_taskDeltaRunSuccessCount) +
									",\"failure\":" + std::to_string(host.m_taskDeltaRunFailureCount) +
									",\"timeout\":" + std::to_string(host.m_taskDeltaRunTimeoutCount) +
									",\"cancelled\":" + std::to_string(host.m_taskDeltaRunCancelledCount) +
									",\"fallback\":" + std::to_string(host.m_taskDeltaRunFallbackCount) + "}" +
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
					bool failed = false;
					bool orchestrationHandled = false;
					bool lifecycleEventsEnqueued = false;
					bool providerStreamed = false;
					const auto orchestrationPolicy =
						ChatOrchestrationPolicy::Evaluate(
							ChatOrchestrationPolicy::Input{
								.orchestrationPath = host.m_embeddedOrchestrationPath,
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
					host.m_latestOrchestrationPathSelection.runId = runId;
					host.m_latestOrchestrationPathSelection.path = orchestrationPath;
					host.m_latestOrchestrationPathSelection.compatDeterministicEnabled =
						allowPromptOrchestration;
					host.m_latestOrchestrationPathSelection.intentDeterministicEnabled =
						forceWeatherEmailDeterministicOrchestration;
					host.m_latestOrchestrationPathSelection.deterministicEnabled =
						allowDeterministicPromptOrchestration;
					host.m_latestOrchestrationPathSelection.decisionReasonCode =
						orchestrationPolicy.decisionReasonCode;
					host.m_latestOrchestrationPathSelection.decompositionMetadataSource =
						orchestrationPolicy.decompositionMetadataSource;
					host.m_latestOrchestrationPathSelection.orderedPolicyMode =
						orchestrationPolicy.orderedPolicyMode;
					host.m_latestOrchestrationPathSelection.orderedPolicyStrict =
						orchestrationPolicy.orderedPolicyStrict;
					host.m_latestOrchestrationPathSelection.fallbackPolicyProfile =
						orchestrationPolicy.fallbackPolicyProfile;
					host.m_latestOrchestrationPathSelection.observedAtEpochMs = nowMs;
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
					const auto runtimeToolsSnapshot = host.RuntimeContext().toolRegistry->List();
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
					const auto orderedSequencePreflight =
						RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
							normalizedMessage,
							runtimeToolsSnapshot,
							host.m_skillsCatalogState.entries,
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

							++host.m_orderedPreflightMissingTargetTotal;

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
								std::to_string(host.m_orderedPreflightMissingTargetTotal) +
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
								++host.m_orderedPreflightMissingTargetTerminalEmittedTotal;
								EmitTelemetryEvent(
									"ordered_preflight_missing_target_terminal_emitted_total",
									std::string("{\"runId\":") + JsonString(runId) +
									",\"total\":" +
									std::to_string(host.m_orderedPreflightMissingTargetTerminalEmittedTotal) +
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
								*host.RuntimeContext().toolRegistry,
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

								EmitTelemetryEvent(
									"gateway.chat.orchestration.execution",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"path\":" +
									JsonString(orchestrationPath) +
									",\"status\":\"success\",\"steps\":" +
									std::to_string(
										orchestrationResult.decompositionSteps) +
									"}");

								auto orchestrationTaskDeltas =
									RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
										{},
										runId,
										sessionKey,
										true,
										assistantText,
										{},
										{});
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

					if (!forceError && !orchestrationHandled && host.m_chatRuntimeCallback) {
						auto& runtimeSessionEvents = host.m_chatEventsBySession[sessionKey];
						PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							});
						PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
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
								*host.RuntimeContext().transport,
								*host.RuntimeContext().eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "queued",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
							EmitPushLifecycleEvent(
								*host.RuntimeContext().transport,
								*host.RuntimeContext().eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "started",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}

						host.m_chatRunsById.insert_or_assign(
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
								.errorCode = {},
								.errorMessage = {},
								.errorContextJson = {},
								.startedAtMs = nowMs,
								.active = true,
								.terminalEventEnqueued = false,
								.pushLifecycleRequested = pushLifecycleEnabled,
								.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
								.originatingChannel = sendControlDecision.route.originatingChannel,
								.originatingTo = sendControlDecision.route.originatingTo,
								.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
							});

						std::size_t streamedDeltaCount = 0;
						const auto runtimeResult = host.m_chatRuntimeCallback(
							GatewayHost::ChatRuntimeRequest{
								.runId = runId,
								.sessionKey = sessionKey,
								.message = runtimeMessage,
								.bodyForCommands = stageContext.bodyForCommands,
								.bodyForAgent = stageContext.bodyForAgent.empty()
									? runtimeMessage
									: stageContext.bodyForAgent,
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
									[&host,
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
												host.m_chatToolEventRecipientsByRun.find(runId);
											if (recipientsIt == host.m_chatToolEventRecipientsByRun.end() ||
												recipientsIt->second.empty()) {
												return;
											}
										}

										auto& streamEvents = host.m_chatEventsBySession[sessionKey];
										PushEventWithRetentionLimit(streamEvents, GatewayHost::ChatEventState{
												.runId = runId,
												.sessionKey = sessionKey,
												.state = "delta",
												.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
												.errorMessage = std::nullopt,
												.timestampMs = CurrentEpochMsLocal(),
											});
										const std::uint64_t deltaNowMs = CurrentEpochMsLocal();
										GatewayLifecycleEventEmitter::EmitLifecycle(
											"delta",
											runId,
											sessionKey,
										 deltaNowMs);

										auto runStateIt = host.m_chatRunsById.find(runId);
										if (runStateIt != host.m_chatRunsById.end() &&
											runStateIt->second.pushLifecycleRequested) {
											EmitPushLifecycleEvent(
												*host.RuntimeContext().transport,
								 *host.RuntimeContext().eventFanout,
												GatewayEventFanoutService::ChatLifecycleEvent{
													.runId = runId,
													.sessionKey = sessionKey,
													.state = "delta",
													.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
													.errorMessage = std::nullopt,
													.timestampMs = deltaNowMs,
												},
												host.m_chatPushEventSeq);
										}

										if (runStateIt != host.m_chatRunsById.end()) {
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
								auto existingRunIt = host.m_chatRunsById.find(runId);
								if (existingRunIt != host.m_chatRunsById.end()) {
									existingRunIt->second.assistantText = assistantText;
									existingRunIt->second.providerDeltas = assistantDeltas;
									existingRunIt->second.providerDeltaCursor = 0;
									existingRunIt->second.streamCursor =
										providerStreamed ? assistantText.size() : 0;
									existingRunIt->second.lastEmitMs = nowMs;
									existingRunIt->second.lastProgressAtMs = nowMs;
									existingRunIt->second.terminalWaitExceededNotified = false;
									existingRunIt->second.failed = failed;
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

						auto existingRunIt = host.m_chatRunsById.find(runId);
						if (existingRunIt != host.m_chatRunsById.end()) {
							existingRunIt->second.assistantText = assistantText;
							existingRunIt->second.providerDeltas = assistantDeltas;
							existingRunIt->second.providerDeltaCursor = 0;
							existingRunIt->second.streamCursor =
								providerStreamed ? assistantText.size() : 0;
							existingRunIt->second.lastEmitMs = nowMs;
							existingRunIt->second.lastProgressAtMs = nowMs;
							existingRunIt->second.terminalWaitExceededNotified = false;
							existingRunIt->second.failed = failed;
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
								*host.RuntimeContext().toolRegistry);

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
					else if (!forceError && !orchestrationHandled && !host.m_chatRuntimeCallback) {
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

					auto& sessionEvents = host.m_chatEventsBySession[sessionKey];
					if (!lifecycleEventsEnqueued) {
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							});
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
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
								*host.RuntimeContext().transport,
								*host.RuntimeContext().eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "queued",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
							EmitPushLifecycleEvent(
								*host.RuntimeContext().transport,
								*host.RuntimeContext().eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "started",
									.messageJson = std::nullopt,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
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
										*host.RuntimeContext().transport,
										*host.RuntimeContext().eventFanout,
										GatewayEventFanoutService::ChatLifecycleEvent{
											.runId = runId,
											.sessionKey = sessionKey,
											.state = "delta",
											.messageJson = deltaMessage,
											.errorMessage = std::nullopt,
											.timestampMs = nowMs,
										},
										host.m_chatPushEventSeq);
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
								emitAssistantDeltaChunk(assistantText, assistantText.size());
							}
							else {
								const std::size_t n =
									(std::min)(assistantText.size(), std::size_t{ 64 });
								emitAssistantDeltaChunk(assistantText.substr(0, n), n);
							}
						}
						else if (!assistantText.empty()) {
							// Phase D: no incremental provider stream — emit one assistant delta with
							// full text instead of synthetic 6-char + poll-simulated streaming.
							emitAssistantDeltaChunk(assistantText, assistantText.size());
						}
					}

					if (host.m_chatRunsById.find(runId) == host.m_chatRunsById.end()) {
						host.m_chatRunsById.insert_or_assign(
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
								.errorCode = backendErrorCode,
								.errorMessage = backendErrorMessage,
								.errorContextJson = backendErrorContextJson,
								.startedAtMs = nowMs,
							 .active = true,
							 .terminalEventEnqueued = false,
								.pushLifecycleRequested = pushLifecycleEnabled,
								.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
								.originatingChannel = sendControlDecision.route.originatingChannel,
								.originatingTo = sendControlDecision.route.originatingTo,
								.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
							});
					}
					auto insertedRunIt = host.m_chatRunsById.find(runId);
					if (insertedRunIt != host.m_chatRunsById.end() &&
						!insertedRunIt->second.failed &&
						!silentAssistantReply &&
						insertedRunIt->second.streamCursor >= insertedRunIt->second.assistantText.size() &&
						!insertedRunIt->second.terminalEventEnqueued) {
						const std::optional<std::string> terminalMessage =
							std::optional<std::string>(
								BuildAssistantFinalMessageJson(insertedRunIt->second.assistantText, nowMs));
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							.runId = insertedRunIt->second.runId,
							.sessionKey = insertedRunIt->second.sessionKey,
							.state = "final",
							.messageJson = terminalMessage,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
							});
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"final",
							insertedRunIt->second.runId,
							insertedRunIt->second.sessionKey,
							nowMs);
						if (insertedRunIt->second.pushLifecycleRequested) {
							EmitPushLifecycleEvent(
								*host.RuntimeContext().transport,
								*host.RuntimeContext().eventFanout,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = insertedRunIt->second.runId,
									.sessionKey = insertedRunIt->second.sessionKey,
									.state = "final",
									.messageJson = terminalMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}
						insertedRunIt->second.terminalEventEnqueued = true;
						insertedRunIt->second.active = false;
						host.RuntimeContext().transportRecipientRegistry->MarkRunFinalized(
							insertedRunIt->second.runId,
							nowMs);
						host.m_chatToolEventRecipientsByRun.erase(insertedRunIt->second.runId);
						host.RuntimeContext().transportRecipientRegistry->PruneExpired(nowMs);
						EmitTelemetryEvent(
							"gateway.chat.final.fastpath",
							std::string("{\"runId\":") +
							JsonString(insertedRunIt->second.runId) +
							",\"sessionKey\":" +
							JsonString(insertedRunIt->second.sessionKey) +
							",\"reason\":\"assistant_text_fully_available\"}");
					}

					host.m_chatTerminalDeliveredRunIds.erase(runId);

					if (!idempotencyKey.empty()) {
						host.m_chatRunByIdempotency.insert_or_assign(idempotencyKey, runId);
					}

					std::string sendPayload =
						"{\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"backendErrorCode\":" +
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
						host.m_chatReplayByIdempotency.insert_or_assign(
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

			host.RuntimeContext().dispatcher->Register(
				"chat.inject",
				[&host](const protocol::RequestFrame& request) {
					const std::string requestedSessionKey =
						ExtractStringParam(request.paramsJson, "sessionKey");
					const std::string sessionKey =
						requestedSessionKey.empty() ? "main" : requestedSessionKey;
					const std::string message =
						ExtractStringParam(request.paramsJson, "message");
					const std::string label =
						ExtractStringParam(request.paramsJson, "label");

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
					auto& queue = host.m_chatEventsBySession[sessionKey];
					PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "final",
							.messageJson = appended.messageJson,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"final",
						runId,
						sessionKey,
						nowMs);

					if (!RuntimeTranscriptGuard::IsSilentReplyText(message)) {
						PushHistoryMessageIfNew(
							host.m_chatHistoryBySession[sessionKey],
							appended.messageJson);
					}

					return protocol::OkResponse(request, "{\"ok\":true,\"messageId\":" +
						JsonString(appended.messageId) + "}");
				});

			host.RuntimeContext().dispatcher->Register(
				"chat.abort",
				[&host](const protocol::RequestFrame& request) {
					const std::string requestedSessionKey =
						ExtractStringParam(request.paramsJson, "sessionKey");
					const std::string sessionKey =
						requestedSessionKey.empty() ? "main" : requestedSessionKey;
					const std::string requestedRunId =
						ExtractStringParam(request.paramsJson, "runId");

					auto runIt = host.m_chatRunsById.end();
					if (!requestedRunId.empty()) {
						const auto exact = host.m_chatRunsById.find(requestedRunId);
						if (exact != host.m_chatRunsById.end() &&
							exact->second.sessionKey == sessionKey) {
							runIt = exact;
						}
					}
					else {
						runIt = std::find_if(
							host.m_chatRunsById.begin(),
							host.m_chatRunsById.end(),
							[&](const auto& pair) {
								return pair.second.sessionKey == sessionKey &&
									pair.second.active;
							});
					}

					if (runIt == host.m_chatRunsById.end()) {
						return protocol::OkResponse(request, "{\"aborted\":false,\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\"}");
					}

					const std::string runId = runIt->second.runId;
					if (host.m_chatAbortCallback) {
						host.m_chatAbortCallback(
							GatewayHost::ChatAbortRequest{
								.runId = runId,
								.sessionKey = sessionKey,
							});
					}

					auto& queue = host.m_chatEventsBySession[sessionKey];
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
					host.m_chatToolEventRecipientsByRun.erase(runId);
					host.RuntimeContext().transportRecipientRegistry->MarkRunFinalized(runId, nowMs);
					host.RuntimeContext().transportRecipientRegistry->PruneExpired(nowMs);

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

			host.RuntimeContext().dispatcher->Register(
				"chat.events.poll",
				[&host, fastSyntheticRevealMode, syntheticRevealMaxDurationMs](
					const protocol::RequestFrame& request) {
						const std::string requestedSessionKey =
							ExtractStringParam(request.paramsJson, "sessionKey");
						const std::string sessionKey =
							requestedSessionKey.empty() ? "main" : requestedSessionKey;
						const std::size_t requestedLimit =
							ExtractSizeParam(request.paramsJson, "limit").value_or(20);
						const std::size_t limit =
							(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 100 }));

						const std::uint64_t nowMs = CurrentEpochMsLocal();
						auto& queue = host.m_chatEventsBySession[sessionKey];
						constexpr std::size_t maxActiveRunsPerPoll = 12;
						constexpr std::uint64_t stalledActiveRunTimeoutMs = 45 * 1000;
						constexpr std::uint64_t terminalWaitExceededThresholdMs = 12 * 1000;

						std::vector<GatewayHost::ChatRunState*> sessionActiveRuns;
						sessionActiveRuns.reserve(host.m_chatRunsById.size());
						for (auto& [_, candidateRun] : host.m_chatRunsById) {
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
								++host.m_chatPollTerminalWaitExceededTotal;
								EmitTelemetryEvent(
									"chat_poll_terminal_wait_exceeded_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(host.m_chatPollTerminalWaitExceededTotal) + "}");
							}

							if (!run.failed &&
								runHasUnfinishedOutput &&
								noProgressElapsedMs >= stalledActiveRunTimeoutMs) {
								++stalledRunCountThisPoll;
								if (reconcileFocusRunId.empty() || noProgressElapsedMs > reconcileFocusElapsedMs) {
									reconcileFocusRunId = run.runId;
									reconcileFocusElapsedMs = noProgressElapsedMs;
								}
								++host.m_chatPollStalledActiveRunTotal;
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
								++host.m_chatPollStalledActiveRunForcedTerminalTotal;
								EmitTelemetryEvent(
									"chat_poll_stalled_active_run_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(host.m_chatPollStalledActiveRunTotal) + "}");
								EmitTelemetryEvent(
									"chat_poll_stalled_active_run_forced_terminal_total",
									std::string("{\"runId\":") + JsonString(run.runId) +
									",\"sessionKey\":" + JsonString(run.sessionKey) +
									",\"elapsedMs\":" + std::to_string(noProgressElapsedMs) +
									",\"total\":" + std::to_string(host.m_chatPollStalledActiveRunForcedTerminalTotal) + "}");
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
													host.m_chatToolEventRecipientsByRun.find(run.runId);
												if (recipientsIt == host.m_chatToolEventRecipientsByRun.end() ||
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
										   .timestampMs = nowMs,
										});
									if (pushLifecycleEnabledForRun) {
										EmitPushLifecycleEvent(
											*host.RuntimeContext().transport,
											*host.RuntimeContext().eventFanout,
											GatewayEventFanoutService::ChatLifecycleEvent{
												.runId = run.runId,
												.sessionKey = run.sessionKey,
												.state = "delta",
												.messageJson = pollDeltaMessage,
												.errorMessage = std::nullopt,
												.timestampMs = nowMs,
											},
											host.m_chatPushEventSeq);
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
									run.failed || silentAssistantReply
									? std::nullopt
									: std::optional<std::string>(
										BuildAssistantFinalMessageJson(run.assistantText, nowMs));
								const std::optional<std::string> terminalError =
									run.failed
									? std::optional<std::string>(run.errorMessage.empty()
										? "chat error"
										: run.errorMessage)
									: std::nullopt;

								PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
									   .runId = run.runId,
									   .sessionKey = run.sessionKey,
									   .state = run.failed ? "error" : "final",
									   .messageJson = terminalMessage,
									   .errorMessage = terminalError,
									   .timestampMs = nowMs,
									});
								if (pushLifecycleEnabledForRun) {
									EmitPushLifecycleEvent(
										*host.RuntimeContext().transport,
										*host.RuntimeContext().eventFanout,
										GatewayEventFanoutService::ChatLifecycleEvent{
											.runId = run.runId,
											.sessionKey = run.sessionKey,
											.state = run.failed ? "error" : "final",
											.messageJson = terminalMessage,
											.errorMessage = terminalError,
											.timestampMs = nowMs,
										},
										host.m_chatPushEventSeq);
								}
								GatewayLifecycleEventEmitter::EmitLifecycle(
									run.failed ? "error" : "final",
									run.runId,
									run.sessionKey,
									nowMs,
									terminalError);
								run.terminalEventEnqueued = true;
								run.lastProgressAtMs = nowMs;
								host.RuntimeContext().transportRecipientRegistry->MarkRunFinalized(
									run.runId,
									nowMs);
								EmitDeepSeekGatewayDiagnostic(
									"event.enqueue",
									std::string("state=") +
									(run.failed ? "error" : "final") +
									" runId=" +
									run.runId +
									" session=" +
									run.sessionKey +
									" queueSize=" +
									std::to_string(queue.size()));

								run.active = false;
								host.m_chatToolEventRecipientsByRun.erase(run.runId);
								host.RuntimeContext().transportRecipientRegistry->PruneExpired(nowMs);
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
									if (host.m_chatTerminalDeliveredRunIds.find(eventState.runId) != host.m_chatTerminalDeliveredRunIds.end() ||
										terminalRunIdsSeenThisPoll.find(eventState.runId) != terminalRunIdsSeenThisPoll.end()) {
										continue;
									}

									terminalRunIdsSeenThisPoll.insert(eventState.runId);
									host.m_chatTerminalDeliveredRunIds.insert(eventState.runId);
									if (host.m_chatTerminalDeliveredRunIds.size() > 1024) {
										host.m_chatTerminalDeliveredRunIds.clear();
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
									const auto runContextIt = host.m_chatRunsById.find(eventState.runId);
									if (runContextIt != host.m_chatRunsById.end()) {
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
									eventState.timestampMs);
								++emitted;

								if ((eventState.state == "final" ||
									eventState.state == "aborted") &&
									eventState.messageJson.has_value() &&
									!IsSilentAssistantMessageJson(eventState.messageJson.value())) {
									PushHistoryMessageIfNew(
										host.m_chatHistoryBySession[sessionKey],
										eventState.messageJson.value());
								}

								if (IsTerminalChatState(eventState.state)) {
									const auto runIt = host.m_chatRunsById.find(eventState.runId);
									if (runIt != host.m_chatRunsById.end()) {
										if (!runIt->second.idempotencyKey.empty()) {
											host.m_chatRunByIdempotency.erase(
												runIt->second.idempotencyKey);
										}

										host.m_chatRunsById.erase(runIt);
										host.RuntimeContext().transportRecipientRegistry->PruneRun(eventState.runId);
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.status",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;

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

			host.RuntimeContext().dispatcher->Register(
				"skills.status",
				[&host](const protocol::RequestFrame& request) {
					protocol::RequestFrame delegated = request;
					delegated.method = "gateway.skills.status";
					return host.RuntimeContext().dispatcher->Dispatch(delegated);
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.install.options",
				[&host](const protocol::RequestFrame& request) {
					std::string optionsJson = "[";
					bool first = true;
					std::size_t count = 0;
					for (const auto& entry : host.m_skillsCatalogState.entries) {
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.install.execute",
				[&host](const protocol::RequestFrame& request) {
					const auto skillName =
						ExtractStringParam(request.paramsJson, "skill");
					const auto it = std::find_if(
						host.m_skillsCatalogState.entries.begin(),
						host.m_skillsCatalogState.entries.end(),
						[&skillName](const SkillsCatalogGatewayEntry& item) {
							return item.name == skillName;
						});

					if (it == host.m_skillsCatalogState.entries.end()) {
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

					const auto warning = host.m_skillsCatalogState.scanCriticalCount > 0
						? "security_scan_critical"
						: (host.m_skillsCatalogState.scanWarnCount > 0
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
						std::to_string(host.m_skillsCatalogState.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(host.m_skillsCatalogState.scanWarnCount) +
						"}");
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.scan.status",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.sandbox.status",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"ok\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"synced\":" +
						std::to_string(state.sandboxSynced) +
						",\"skipped\":" +
						std::to_string(state.sandboxSkipped) +
						"}");
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.env.status",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}");
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.config.schema.get",
				[&host](const protocol::RequestFrame& request) {
					if (!host.m_configSchemaGetCallback) {
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

					const auto state = host.m_configSchemaGetCallback();
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.config.schema.lookup",
				[&host](const protocol::RequestFrame& request) {
					if (!host.m_configSchemaLookupCallback) {
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
						host.m_configSchemaLookupCallback(normalizedPath.value());
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.info",
				[&host](const protocol::RequestFrame& request) {
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
						host.m_skillsCatalogState.entries.begin(),
						host.m_skillsCatalogState.entries.end(),
						[&skillName](const SkillsCatalogGatewayEntry& entry) {
							return entry.name == skillName ||
								entry.skillKey == skillName;
						});

					if (it == host.m_skillsCatalogState.entries.end()) {
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
						std::to_string(host.m_skillsCatalogState.scanCriticalCount) +
						"}");
				});

			// Skills update: no param parsing here; forward to host.m_skillsUpdateCallback (BlazeClaw:
			// SkillsGatewayMethodHandler::HandleSkillsUpdate). Alias "skills.update" below rewrites method only.
			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.update",
				[&host](const protocol::RequestFrame& request) {
					if (!host.m_skillsUpdateCallback) {
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

					return host.m_skillsUpdateCallback(request);
				});

			host.RuntimeContext().dispatcher->Register(
				"skills.update",
				[&host](const protocol::RequestFrame& request) {
					protocol::RequestFrame delegated = request;
					delegated.method = "gateway.skills.update";
					if (!host.m_skillsUpdateCallback) {
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

					return host.m_skillsUpdateCallback(delegated);
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.check",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.diagnostics",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.prompt",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;

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

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.commands",
				[&host](const protocol::RequestFrame& request) {
					std::string commandsJson = "[";
					bool first = true;
					std::size_t count = 0;

					for (const auto& entry : host.m_skillsCatalogState.entries) {
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

			host.RuntimeContext().dispatcher->Register(
				"skills.commands",
				[&host](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = "gateway.skills.commands";
					return host.RuntimeContext().dispatcher->Dispatch(forwarded);
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.refresh",
				[&host](const protocol::RequestFrame& request) {
					bool refreshed = false;
					if (host.m_skillsRefreshCallback) {
						host.m_skillsCatalogState = host.m_skillsRefreshCallback();
						refreshed = true;
					}

					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"refreshed\":" +
						std::string(refreshed ? "true" : "false") +
						",\"version\":" +
						std::to_string(state.snapshotVersion) +
						",\"reason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"}");
				});

			host.RuntimeContext().dispatcher->Register(
				"gateway.skills.list",
				[&host](const protocol::RequestFrame& request) {
					const auto includeInvalid =
						ExtractBoolParam(request.paramsJson, "includeInvalid");
					const bool shouldIncludeInvalid =
						!includeInvalid.has_value() || includeInvalid.value();

					std::string entriesJson = "[";
					bool first = true;
					std::size_t count = 0;
					for (const auto& entry : host.m_skillsCatalogState.entries) {
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
