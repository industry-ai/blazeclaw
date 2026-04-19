#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayHostRuntimeLocalHelpers.h"
#include "GatewayJsonUtils.h"
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

		host.m_dispatcher.Register(
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
				auto pipelineResult = host.m_chatRunPipelineOrchestrator.Run(stageContext);
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

					if (!normalizedMessage.empty() || hasAttachments) {
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

					PushHistoryMessageIfNew(
						host.m_chatHistoryBySession[sessionKey],
						BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));
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
					host.m_transportRecipientRegistry.HasRecipients(runId);
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
					host.m_transportRecipientRegistry.RegisterRecipient(
						runId,
						sessionKey,
						clientConnectionId,
						nowMs);
					host.m_transportRecipientRegistry.RegisterLateJoin(
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
					host.m_transportRecipientRegistry.PruneExpired(nowMs);
				}

				if (lateJoinRequested && !clientConnectionId.empty()) {
					auto& replayQueue = host.m_chatEventsBySession[sessionKey];
					const auto activeRuns =
						host.m_transportRecipientRegistry.ActiveRunsForSession(sessionKey);
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
							host.m_taskDeltaRepository.EnforceRetentionLimit(
								host.m_taskDeltasRetentionLimit);
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

							if (host.m_taskDeltaRepository.Size() > host.m_taskDeltasRetentionLimit) {
								host.m_taskDeltaRepository.EnforceRetentionLimit(
									host.m_taskDeltasRetentionLimit);
								host.PersistTaskDeltas();
							}
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
				const auto runtimeToolsSnapshot = host.m_toolRegistry.List();
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

									deltas.push_back(
										"tools.execute.result tool=" +
										delta.toolName +
										" status=" +
										(delta.status.empty() ? std::string("ok") : delta.status) +
										(delta.errorCode.empty()
											? std::string()
											: (" errorCode=" + delta.errorCode)));
									continue;
								}
							}

							return deltas;
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
						const std::string strictMissingErrorMessage =
							"Ordered execution preflight failed. Missing targets: " +
							RuntimeSequencingPolicy::JoinOrderedTargets(
								orderedSequencePreflight.missingTargets);

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
							",\"layer\":\"ordered_preflight\",\"reason\":\"strict_missing\",\"recoveryRoute\":" +
							JsonString(orderedRecoveryOutcome.recoveryRoute) + "}");

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
							assistantText =
								(preferChineseResponse
									? (Utf8LiteralLocal(u8"\u65E0\u6CD5\u6267\u884C\u6709\u5E8F\u5DE5\u4F5C\u6D41\uFF0C\u4EE5\u4E0B\u6B65\u9AA4\u76EE\u6807\u4E0D\u53EF\u7528\uFF1A") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
										Utf8LiteralLocal(u8"\u3002\u8BF7\u5B89\u88C5\u6216\u542F\u7528\u8FD9\u4E9B\u6280\u80FD/\u5DE5\u5177\u540E\u91CD\u8BD5\u3002"))
									: (std::string("Unable to execute the ordered workflow because required step targets are unavailable: ") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
										". Please install or enable these skills/tools and retry."));

							auto blockedTaskDeltas =
								RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
									runId,
									sessionKey,
									orderedSequencePreflight,
									true,
									backendErrorCode,
									backendErrorMessage);
							assistantDeltas =
								buildAssistantDeltasFromTaskDeltas(blockedTaskDeltas);
							if (assistantDeltas.empty()) {
								assistantDeltas.push_back(assistantText);
							}

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
							host.m_toolRegistry,
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
							host.m_transport,
							host.m_eventFanoutService,
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
							host.m_transport,
							host.m_eventFanoutService,
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
							.failed = false,
							.errorMessage = {},
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
											host.m_transport,
							 host.m_eventFanoutService,
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
								existingRunIt->second.failed = failed;
								existingRunIt->second.errorMessage = backendErrorMessage;
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
						existingRunIt->second.failed = failed;
						existingRunIt->second.errorMessage = backendErrorMessage;
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
							host.m_toolRegistry);

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

					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

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
					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

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
							host.m_transport,
							host.m_eventFanoutService,
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
							host.m_transport,
							host.m_eventFanoutService,
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
					streamCursor = (std::min)(assistantText.size(), std::size_t{ 6 });
					if (streamCursor > 0) {
						const std::string initialDeltaMessage = BuildAssistantDeltaMessageJson(
							assistantText.substr(0, streamCursor));
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							   .runId = runId,
							   .sessionKey = sessionKey,
							   .state = "delta",
							  .messageJson = initialDeltaMessage,
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
								host.m_transport,
								host.m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "delta",
									.messageJson = initialDeltaMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}
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
							.failed = failed,
							.errorMessage = backendErrorMessage,
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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
				host.m_chatToolEventRecipientsByRun.erase(runId);
				host.m_transportRecipientRegistry.MarkRunFinalized(runId, nowMs);
				host.m_transportRecipientRegistry.PruneExpired(nowMs);

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

		host.m_dispatcher.Register(
			"chat.events.poll",
			[&host](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::size_t requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 100 }));

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				auto queueIt = host.m_chatEventsBySession.find(sessionKey);
				auto& queue = host.m_chatEventsBySession[sessionKey];

				auto runIt = std::find_if(
					host.m_chatRunsById.begin(),
					host.m_chatRunsById.end(),
					[&](auto& pair) {
						return pair.second.sessionKey == sessionKey && pair.second.active;
					});

				if (runIt != host.m_chatRunsById.end()) {
					auto& run = runIt->second;
					const bool pushLifecycleEnabledForRun =
						run.pushLifecycleRequested;
					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(run.assistantText);
					const bool enoughTimeElapsed =
						run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;

					if (!run.failed && !silentAssistantReply &&
						((run.providerDeltaCursor < run.providerDeltas.size()) ||
							(run.streamCursor < run.assistantText.size())) &&
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
						std::string pollDeltaMessage;
						if (run.providerDeltaCursor < run.providerDeltas.size()) {
							while (run.providerDeltaCursor < run.providerDeltas.size()) {
								deltaText = run.providerDeltas[run.providerDeltaCursor];
								++run.providerDeltaCursor;
								if (controlPlaneService.ShouldPublishToolDelta(
									deltaText,
									pollDecision)) {
									if (deltaText.find("tools.execute") == 0) {
										const auto recipientsIt =
											host.m_chatToolEventRecipientsByRun.find(run.runId);
										if (recipientsIt == host.m_chatToolEventRecipientsByRun.end() ||
											recipientsIt->second.empty()) {
											deltaText.clear();
											continue;
										}
									}
									break;
								}
								deltaText.clear();
							}
							if (deltaText.empty()) {
								goto skip_poll_delta_emit;
							}
							run.streamCursor = deltaText.size();
						}
						else {
							const std::size_t nextCursor =
								(std::min)(run.assistantText.size(), run.streamCursor + std::size_t{ 8 });
							run.streamCursor = nextCursor;
							deltaText = run.assistantText.substr(0, run.streamCursor);
						}

						run.lastEmitMs = nowMs;
						pollDeltaMessage = BuildAssistantDeltaMessageJson(deltaText);

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
								host.m_transport,
								host.m_eventFanoutService,
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
					skip_poll_delta_emit:;
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
								host.m_transport,
								host.m_eventFanoutService,
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
						host.m_transportRecipientRegistry.MarkRunFinalized(
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
						host.m_transportRecipientRegistry.PruneExpired(nowMs);
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

						eventsJson += BuildChatEventJson(
							eventState.runId,
							eventState.sessionKey,
							eventState.state,
							eventState.messageJson,
							eventState.errorMessage,
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
								host.m_transportRecipientRegistry.PruneRun(eventState.runId);
							}
						}
					}
				}

				eventsJson += "]";
				EmitDeepSeekGatewayDiagnostic(
					"event.dequeue",
					std::string("poll complete session=") +
					sessionKey +
					" emitted=" +
					std::to_string(emitted) +
					" queueRemaining=" +
					std::to_string(queue.size()));
				return protocol::OkResponse(request, "{\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\",\"events\":" +
						eventsJson +
						",\"count\":" +
						std::to_string(emitted) +
						"}");
			});

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
			"gateway.skills.env.status",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}");
			});

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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
		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
			"gateway.skills.check",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				const bool ok =
					state.scanCriticalCount == 0 &&
					state.installBlockedCount == 0 &&
					state.sandboxSyncOk;

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
						"}");
			});

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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

		host.m_dispatcher.Register(
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
