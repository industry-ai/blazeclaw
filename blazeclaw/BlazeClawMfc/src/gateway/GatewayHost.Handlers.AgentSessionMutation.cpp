#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersAgentSessionMutation.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "GatewaySessionUtilsService.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::agent_session_mutation {

	void AgentSessionMutationHandlers::RegisterAll(GatewayHost& host) {
		host.m_dispatcher.Register("gateway.agents.update", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedName = RequestParamsView(request.paramsJson).GetString("name");
			const std::optional<bool> requestedActive = RequestParamsView(request.paramsJson).GetBool("active");
			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.update::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != host.m_mutationPayloadByIdempotency.end()) {
					return protocol::OkResponse(request, dedupeIt->second);
				}
			}

			const AgentEntry updated = host.m_agentRegistry.Update(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);
			const std::string payload = "{\"agent\":" + SerializeAgent(updated) + ",\"updated\":true}";

			if (!idempotencyKey.empty()) {
				host.m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.agents.delete", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.delete::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != host.m_mutationPayloadByIdempotency.end()) {
					return protocol::OkResponse(request, dedupeIt->second);
				}
			}

			AgentEntry removedAgent;
			const bool deleted = host.m_agentRegistry.Delete(requestedId, removedAgent);
			const std::size_t remaining = host.m_agentRegistry.List().size();
			const std::string payload =
				"{\"agent\":" + SerializeAgent(removedAgent) +
				",\"deleted\":" + std::string(deleted ? "true" : "false") +
				",\"remaining\":" + std::to_string(remaining) + "}";

			if (!idempotencyKey.empty()) {
				host.m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.agents.create", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedName = RequestParamsView(request.paramsJson).GetString("name");
			const std::optional<bool> requestedActive = RequestParamsView(request.paramsJson).GetBool("active");
			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.create::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != host.m_mutationPayloadByIdempotency.end()) {
					return protocol::OkResponse(request, dedupeIt->second);
				}
			}

			const AgentEntry created = host.m_agentRegistry.Create(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);
			const std::string payload = "{\"agent\":" + SerializeAgent(created) + ",\"created\":true}";

			if (!idempotencyKey.empty()) {
				host.m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.sessions.patch", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const std::string requestedScope = RequestParamsView(request.paramsJson).GetString("scope");
			const std::optional<bool> requestedActive = RequestParamsView(request.paramsJson).GetBool("active");
			const auto patchResult = GatewaySessionUtilsService::PatchSessionAcrossStores(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			const SessionEntry patched = patchResult.session;
			EmitTelemetryEvent(
				"gateway.event.sessions.changed",
				JsonObject({
					{"event", JsonString("sessions.changed")},
					{"sessionId", JsonString(patched.id)},
					{"action", JsonString("patch")},
					}));

			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(patched) + ",\"patched\":true}");
			});

		host.m_dispatcher.Register("gateway.sessions.preview", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const SessionEntry session = GatewaySessionUtilsService::ResolveFreshestSessionAcrossStores(requestedId)
				.value_or(host.m_sessionRegistry.Resolve(requestedId));
			const auto payload = GatewaySessionUtilsService::BuildSessionPreviewPayload(
				session,
				GatewayModel::ResolveModelProvider(host.m_runtimeAgentModel),
				host.m_runtimeAgentModel);
			EmitTelemetryEvent(
				"gateway.event.sessions.preview.fallback",
				JsonObject({
					{"event", JsonString("sessions.preview.fallback")},
					{"sessionId", JsonString(session.id)},
					{"fallbackSource", JsonString(payload.fallbackSource)},
					{"totalTokensFresh", JsonBool(payload.totalTokensFresh)},
				}));
			return protocol::OkResponse(request, payload.json);
			});

		host.m_dispatcher.Register("gateway.sessions.compact", [&host](const protocol::RequestFrame& request) {
			const bool dryRun = RequestParamsView(request.paramsJson).GetBool("dryRun").value_or(false);
			const std::size_t compacted = dryRun ? host.m_sessionRegistry.CountCompactCandidates() : host.m_sessionRegistry.CompactInactive();
			const std::size_t remaining = host.m_sessionRegistry.List().size();

			return protocol::OkResponse(request, "{\"compacted\":" + std::to_string(compacted) +
				",\"remaining\":" + std::to_string(remaining) +
				",\"dryRun\":" + std::string(dryRun ? "true" : "false") + "}");
			});

		auto registerSessionsUsage = [&host](const std::string& methodName, bool openClawEnvelope) {
			host.m_dispatcher.Register(methodName, [&host, openClawEnvelope](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string requestedId = params.GetString("sessionId");
				const std::string requestedStartDate = params.GetString("startDate");
				const std::string requestedEndDate = params.GetString("endDate");
				const SessionEntry session = GatewaySessionUtilsService::ResolveFreshestSessionAcrossStores(requestedId)
					.value_or(host.m_sessionRegistry.Resolve(requestedId));
				const auto payload = GatewaySessionUtilsService::BuildSessionUsagePayload(
					session,
					GatewayModel::ResolveModelProvider(host.m_runtimeAgentModel),
					host.m_runtimeAgentModel,
					requestedStartDate,
					requestedEndDate,
					openClawEnvelope);
				EmitTelemetryEvent(
					"gateway.event.sessions.usage.fallback",
					JsonObject({
						{"event", JsonString("sessions.usage.fallback")},
						{"sessionId", JsonString(session.id)},
						{"fallbackSource", JsonString(payload.fallbackSource)},
						{"totalTokensFresh", JsonBool(payload.totalTokensFresh)},
					}));
				return protocol::OkResponse(request, payload.json);
				});
			};
		registerSessionsUsage("gateway.sessions.usage", false);
		registerSessionsUsage("sessions.usage", true);

		host.m_dispatcher.Register("gateway.sessions.delete", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const auto deleteResult = GatewaySessionUtilsService::DeleteSessionAcrossStores(requestedId);
			const SessionEntry removedSession = deleteResult.session;
			const bool deleted = deleteResult.changed;
			const std::size_t remaining = GatewaySessionUtilsService::ListMergedSessions().size();
			if (deleted) {
				EmitTelemetryEvent(
					"gateway.event.sessions.changed",
					JsonObject({
						{"event", JsonString("sessions.changed")},
						{"sessionId", JsonString(removedSession.id)},
						{"action", JsonString("delete")},
						}));
			}

			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(removedSession) +
				",\"deleted\":" + std::string(deleted ? "true" : "false") +
				",\"remaining\":" + std::to_string(remaining) + "}");
			});

		host.m_dispatcher.Register("gateway.features.list", [&host](const protocol::RequestFrame& request) {
			const std::string methodsJson = SerializeStringArray(host.m_dispatcher.RegisteredMethods());
			const std::string eventsJson = SerializeStringArray(GatewayEventCatalogNames());

			return protocol::OkResponse(request, "{\"methods\":" + methodsJson + ",\"events\":" + eventsJson + "}");
			});

		auto registerAgentsList = [&host](const std::string& methodName) {
			host.m_dispatcher.Register(methodName, [&host](const protocol::RequestFrame& request) {
				const std::optional<bool> activeFilter = RequestParamsView(request.paramsJson).GetBool("active");
				const auto agents = host.m_agentRegistry.List();
				std::string payload = "{\"agents\":[";
				bool first = true;
				std::string activeAgentId = "none";
				std::size_t count = 0;
				for (std::size_t i = 0; i < agents.size(); ++i) {
					if (activeFilter.has_value() && agents[i].active != activeFilter.value()) {
						continue;
					}

					if (!first) {
						payload += ",";
					}

					payload += SerializeAgent(agents[i]);
					if (activeAgentId == "none" && agents[i].active) {
						activeAgentId = agents[i].id;
					}

					first = false;
					++count;
				}

				payload += "],\"count\":" + std::to_string(count) + ",\"activeAgentId\":\"" + EscapeJsonString(activeAgentId) + "\"}";

				return protocol::OkResponse(request, payload);
				});
			};
		registerAgentsList("gateway.agents.list");
		registerAgentsList("agents.list");

		host.m_dispatcher.Register("gateway.agents.run", [&host](const protocol::RequestFrame& request) {
			const std::string requestedAgentId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedSessionId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const std::string message = RequestParamsView(request.paramsJson).GetString("message");
			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");

			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_agentRunByIdempotency.find(idempotencyKey);
				if (dedupeIt != host.m_agentRunByIdempotency.end()) {
					const auto runIt = host.m_agentRuns.find(dedupeIt->second);
					if (runIt != host.m_agentRuns.end()) {
						const auto& run = runIt->second;
						const std::string completedMsJson = run.completedAtMs.has_value()
							? std::to_string(run.completedAtMs.value())
							: "null";
						return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonString(run.runId) +
							"\",\"status\":\"" + EscapeJsonString(run.status) +
							"\",\"agentId\":\"" + EscapeJsonString(run.agentId) +
							"\",\"sessionId\":\"" + EscapeJsonString(run.sessionId) +
							"\",\"summary\":\"" + EscapeJsonString(run.summary) +
							"\",\"deduped\":true,\"startedAtMs\":" +
							std::to_string(run.startedAtMs) +
							",\"completedAtMs\":" + completedMsJson + "}");
					}
				}
			}

			const AgentEntry agent = host.m_agentRegistry.Get(requestedAgentId);
			const SessionEntry session = host.m_sessionRegistry.Resolve(requestedSessionId);
			const std::uint64_t startedAtMs = GatewayEpochMilliseconds();
			const std::string runId =
				"run-" + std::to_string(startedAtMs) + "-" + agent.id;

			std::string chatParamsJson =
				"{\"sessionKey\":\"" + EscapeJsonString(session.id) +
				"\",\"message\":\"" + EscapeJsonString(message) + "\"";
			if (!idempotencyKey.empty()) {
				chatParamsJson +=
					",\"idempotencyKey\":\"agents-run::" +
					EscapeJsonString(idempotencyKey) + "\"";
			}
			chatParamsJson += "}";

			const protocol::RequestFrame chatRequest{
				.id = runId,
				.method = "chat.send",
				.paramsJson = chatParamsJson,
			};
			const protocol::ResponseFrame chatResponse =
				host.RouteRequest(chatRequest);
			if (!chatResponse.ok || !chatResponse.payloadJson.has_value()) {
				if (chatResponse.error.has_value()) {
					return protocol::ErrorResponse(request, std::move(*chatResponse.error));
				}
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "chat_dispatch_failed",
						.message = "Unable to start chat runtime flow.",
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}

			std::string chatRunId;
			json::FindStringField(
				chatResponse.payloadJson.value(),
				"runId",
				chatRunId);
			if (chatRunId.empty()) {
				chatRunId = runId;
			}

			std::string backendErrorCode;
			json::FindStringField(
				chatResponse.payloadJson.value(),
				"backendErrorCode",
				backendErrorCode);

			GatewayHost::AgentRunState run{
				.runId = chatRunId,
				.agentId = agent.id,
				.sessionId = session.id,
				.message = message,
				.status = "queued",
				.summary = backendErrorCode.empty() ? "queued" : backendErrorCode,
				.startedAtMs = startedAtMs,
				.completedAtMs = std::nullopt,
			};

			host.m_agentRuns.insert_or_assign(chatRunId, run);
			if (!idempotencyKey.empty()) {
				host.m_agentRunByIdempotency.insert_or_assign(idempotencyKey, chatRunId);
			}

			return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonString(run.runId) +
				"\",\"status\":\"" + EscapeJsonString(run.status) +
				"\",\"agentId\":\"" + EscapeJsonString(run.agentId) +
				"\",\"sessionId\":\"" + EscapeJsonString(run.sessionId) +
				"\",\"summary\":\"" + EscapeJsonString(run.summary) +
				"\",\"deduped\":false,\"startedAtMs\":" +
				std::to_string(run.startedAtMs) +
				",\"completedAtMs\":null}");
			});

		host.m_dispatcher.Register("gateway.agents.wait", [&host](const protocol::RequestFrame& request) {
			const std::string runId = RequestParamsView(request.paramsJson).GetString("runId");
			const auto runIt = host.m_agentRuns.find(runId);
			if (runIt == host.m_agentRuns.end()) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "run_not_found",
						.message = "Agent run was not found.",
						.detailsJson = "{\"runId\":\"" + EscapeJsonString(runId) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}

			auto& run = runIt->second;
			const auto chatRunIt = host.m_chatRunsById.find(runId);
			if (chatRunIt != host.m_chatRunsById.end()) {
				const auto& chatRun = chatRunIt->second;
				if (chatRun.failed) {
					run.status = "failed";
					run.summary = chatRun.errorMessage.empty()
						? "chat_runtime_error"
						: chatRun.errorMessage;
				}
				else if (chatRun.active) {
					run.status = "running";
					run.summary = "running";
				}
				else {
					run.status = "completed";
					run.summary = "completed";
				}

				if (!chatRun.active && !run.completedAtMs.has_value()) {
					run.completedAtMs = GatewayEpochMilliseconds();
				}
			}
			else {
				const auto taskDeltasIt = host.m_taskDeltasByRunId.find(runId);
				if (taskDeltasIt != host.m_taskDeltasByRunId.end() &&
					!taskDeltasIt->second.empty()) {
					auto orderedTaskDeltas = taskDeltasIt->second;
					std::sort(
						orderedTaskDeltas.begin(),
						orderedTaskDeltas.end(),
						[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& left,
							const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& right) {
								return left.index < right.index;
						});

					for (auto it = orderedTaskDeltas.rbegin();
						it != orderedTaskDeltas.rend();
						++it) {
						if (it->phase != "final") {
							continue;
						}

						run.status = it->status.empty() ? "completed" : it->status;
						run.summary = (run.status == "failed")
							? (it->errorCode.empty() ? "failed" : it->errorCode)
							: "completed";
						if (!run.completedAtMs.has_value()) {
							run.completedAtMs = it->completedAtMs > 0
								? it->completedAtMs
								: GatewayEpochMilliseconds();
						}
						break;
					}
				}

				if (host.m_chatTerminalDeliveredRunIds.find(runId) !=
					host.m_chatTerminalDeliveredRunIds.end()) {
					if (run.status == "queued" || run.status == "running") {
						run.status = "completed";
						run.summary = "completed";
					}

					if (!run.completedAtMs.has_value()) {
						run.completedAtMs = GatewayEpochMilliseconds();
					}
				}
			}

			const std::string completedMsJson = run.completedAtMs.has_value()
				? std::to_string(run.completedAtMs.value())
				: "null";
			const std::string terminal = run.completedAtMs.has_value() ||
				run.status == "completed" ||
				run.status == "failed" ||
				run.status == "aborted"
				? "true"
				: "false";

			return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonString(run.runId) +
				"\",\"status\":\"" + EscapeJsonString(run.status) +
				"\",\"summary\":\"" + EscapeJsonString(run.summary) +
				"\",\"terminal\":" + terminal +
				",\"agentId\":\"" + EscapeJsonString(run.agentId) +
				"\",\"sessionId\":\"" + EscapeJsonString(run.sessionId) +
				"\",\"startedAtMs\":" + std::to_string(run.startedAtMs) +
				",\"completedAtMs\":" + completedMsJson + "}");
			});
	}

} // namespace blazeclaw::gateway::handlers::agent_session_mutation

namespace blazeclaw::gateway {

	void GatewayHost::RegisterGatewayAgentSessionMutationHandlers() {
		handlers::agent_session_mutation::AgentSessionMutationHandlers::RegisterAll(*this);
	}

} // namespace blazeclaw::gateway
