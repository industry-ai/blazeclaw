#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRegistryIntrospection.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::registry_introspection {

	void RegistryIntrospectionHandlers::RegisterAll(GatewayHost& host) {
		host.m_dispatcher.Register("usage.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"status\":\"ok\",\"healthy\":true,\"updatedAt\":1735689600200}");
			});

		host.m_dispatcher.Register("usage.cost", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"currency\":\"USD\",\"daily\":[],\"totalCost\":0.0}");
			});

		host.m_dispatcher.Register("sessions.usage.logs", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"logs\":[]}");
			});

		host.m_dispatcher.Register("sessions.usage.timeseries", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"points\":[],\"totals\":{\"input\":0,\"output\":0,\"cacheRead\":0,\"cacheWrite\":0,\"totalTokens\":0,\"totalCost\":0.0}}}");
			});

		host.m_dispatcher.Register("gateway.agents.exists", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const auto agents = host.m_agentRegistry.List();
			const bool exists = std::any_of(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return requestedId.empty() || agent.id == requestedId;
				});

			return protocol::OkResponse(request, "{\"agentId\":\"" + EscapeJsonString(requestedId.empty() ? "*" : requestedId) +
				"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.config.getSection", [&host](const protocol::RequestFrame& request) {
			const std::string section = RequestParamsView(request.paramsJson).GetString("section");
			const std::string resolved = section.empty() ? "gateway" : section;
			std::string sectionJson = "{\"bind\":\"" + EscapeJsonString(host.m_runtimeGatewayBind) + "\",\"port\":" + std::to_string(host.m_runtimeGatewayPort) + "}";
			if (resolved == "agent") {
				sectionJson = "{\"model\":\"" + EscapeJsonString(host.m_runtimeAgentModel) + "\",\"streaming\":" + std::string(host.m_runtimeAgentStreaming ? "true" : "false") + "}";
			}
			else if (resolved == "deepseek") {
				sectionJson = BuildGatewayDeepSeekConfigJson(
					host.m_runtimeDeepSeekApiKey,
					host.m_runtimeDeepSeekBaseUrl,
					host.m_runtimeDeepSeekDefaultModel);
			}

			return protocol::OkResponse(request, "{\"section\":\"" + EscapeJsonString(resolved) + "\",\"config\":" + sectionJson + "}");
			});

		host.m_dispatcher.Register("gateway.tools.get", [&host](const protocol::RequestFrame& request) {
			const std::string requestedTool = RequestParamsView(request.paramsJson).GetString("tool");
			const auto tools = host.m_toolRegistry.List();
			ToolCatalogEntry selected{};
			if (!tools.empty()) {
				selected = tools.front();
			}
			for (const auto& tool : tools) {
				if (!requestedTool.empty() && tool.id != requestedTool) {
					continue;
				}
				selected = tool;
				break;
			}

			return protocol::OkResponse(request, "{\"tool\":" + SerializeTool(selected) + "}");
			});

		host.m_dispatcher.Register("gateway.agents.count", [&host](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = RequestParamsView(request.paramsJson).GetBool("active");
			const auto agents = host.m_agentRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return !activeFilter.has_value() || agent.active == activeFilter.value();
				}));

			return protocol::OkResponse(request, "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
				",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
				",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.models.get", [](const protocol::RequestFrame& request) {
			const std::string modelId = RequestParamsView(request.paramsJson).GetString("modelId");
			const std::string resolvedId = GatewayModel::NormalizeModelId(modelId);

			return protocol::OkResponse(request, "{\"model\":" + GatewayModel::BuildModelJson(resolvedId) + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.exists", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const auto sessions = host.m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});

			return protocol::OkResponse(request, "{\"sessionId\":\"" + EscapeJsonString(requestedId.empty() ? "*" : requestedId) +
				"\",\"exists\":" + std::string(exists ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.config.getKey", [&host](const protocol::RequestFrame& request) {
			const std::string key = RequestParamsView(request.paramsJson).GetString("key");
			std::string value;
			if (key == "gateway.bind") {
				value = host.m_runtimeGatewayBind;
			}
			else if (key == "gateway.port") {
				value = std::to_string(host.m_runtimeGatewayPort);
			}
			else if (key == "agent.model") {
				value = host.m_runtimeAgentModel;
			}
			else if (key == "agent.streaming") {
				value = host.m_runtimeAgentStreaming ? "true" : "false";
			}
			else if (key == "deepseek.apiKey") {
				value = MaskGatewaySecret(host.m_runtimeDeepSeekApiKey);
			}
			else if (key == "deepseek.baseUrl") {
				value = host.m_runtimeDeepSeekBaseUrl;
			}
			else if (key == "deepseek.defaultModel") {
				value = host.m_runtimeDeepSeekDefaultModel;
			}

			return protocol::OkResponse(request, "{\"key\":\"" + EscapeJsonString(key.empty() ? "gateway.bind" : key) +
				"\",\"value\":\"" + EscapeJsonString(value.empty() ? host.m_runtimeGatewayBind : value) + "\"}");
			});

		host.m_dispatcher.Register("gateway.sessions.count", [&host](const protocol::RequestFrame& request) {
			const std::string scope = RequestParamsView(request.paramsJson).GetString("scope");
			const std::optional<bool> active = RequestParamsView(request.paramsJson).GetBool("active");
			const auto sessions = host.m_sessionRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				if (!scope.empty() && session.scope != scope) {
					return false;
				}
				if (active.has_value() && session.active != active.value()) {
					return false;
				}
				return true;
				}));

			return protocol::OkResponse(request, "{\"scope\":\"" + EscapeJsonString(scope.empty() ? "*" : scope) +
				"\",\"count\":" + std::to_string(count) + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.activate", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("sessionId");
			const auto sessions = host.m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});
			const SessionEntry activated = host.m_sessionRegistry.Patch(requestedId, std::nullopt, true);

			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(activated) +
				",\"activated\":" + std::string(exists ? "true" : "false") + "}");
			});


		host.m_dispatcher.Register("gateway.agents.files.exists", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedPath = RequestParamsView(request.paramsJson).GetString("path");
			const AgentFileExistsResult result = host.m_agentRegistry.ExistsFile(requestedId, requestedPath);

			return protocol::OkResponse(request, JsonPayloadPathExists(result.path, result.exists));
			});

		host.m_dispatcher.Register("gateway.agents.files.delete", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedPath = RequestParamsView(request.paramsJson).GetString("path");
			if (IsUnsafeGatewayAgentFilePath(requestedPath)) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJsonString(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}

			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.files.delete::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != host.m_mutationPayloadByIdempotency.end()) {
					return protocol::OkResponse(request, dedupeIt->second);
				}
			}

			const AgentFileDeleteResult result = host.m_agentRegistry.DeleteFile(requestedId, requestedPath);
			const std::string payload =
				"{\"file\":" + SerializeAgentFileContent(result.file) +
				",\"deleted\":" + std::string(result.deleted ? "true" : "false") + "}";

			if (!idempotencyKey.empty()) {
				host.m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.agents.files.set", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedPath = RequestParamsView(request.paramsJson).GetString("path");
			if (IsUnsafeGatewayAgentFilePath(requestedPath)) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJsonString(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}

			const std::string content = RequestParamsView(request.paramsJson).GetString("content");
			const std::string idempotencyKey = RequestParamsView(request.paramsJson).GetString("idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.files.set::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = host.m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != host.m_mutationPayloadByIdempotency.end()) {
					return protocol::OkResponse(request, dedupeIt->second);
				}
			}

			const AgentFileContentEntry file = host.m_agentRegistry.SetFile(requestedId, requestedPath, content);
			const std::string payload = "{\"file\":" + SerializeAgentFileContent(file) + ",\"saved\":true}";

			if (!idempotencyKey.empty()) {
				host.m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.agents.files.get", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const std::string requestedPath = RequestParamsView(request.paramsJson).GetString("path");
			if (IsUnsafeGatewayAgentFilePath(requestedPath)) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJsonString(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}
			const AgentFileContentEntry file = host.m_agentRegistry.GetFile(requestedId, requestedPath);

			return protocol::OkResponse(request, "{\"file\":" + SerializeAgentFileContent(file) + "}");
			});

		host.m_dispatcher.Register("gateway.agents.files.list", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const auto files = host.m_agentRegistry.ListFiles(requestedId);
			std::string filesJson = "[";
			for (std::size_t i = 0; i < files.size(); ++i) {
				if (i > 0) {
					filesJson += ",";
				}

				filesJson += SerializeAgentFile(files[i]);
			}

			filesJson += "]";

			return protocol::OkResponse(request, "{\"files\":" + filesJson + ",\"count\":" + std::to_string(files.size()) + "}");
			});

		host.m_dispatcher.Register("gateway.models.list", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"models\":[" +
				GatewayModel::BuildModelJson(GatewayModel::kDefaultModelId) + "," +
				GatewayModel::BuildModelJson(GatewayModel::kReasonerModelId) + "," +
				GatewayModel::BuildModelJson(GatewayModel::kDeepSeekChatModelId) + "," +
				GatewayModel::BuildModelJson(GatewayModel::kDeepSeekReasonerModelId) + "]}");
			});

		host.m_dispatcher.Register("models.list", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.models.list";
			return host.m_dispatcher.Dispatch(forwarded);
			});

		host.m_dispatcher.Register("gateway.tools.call.execute", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView paramsView(request.paramsJson);
			const std::string requestedTool = paramsView.GetString("tool");
			const auto argsResolution = paramsView.ResolveToolExecuteArgs();
			const std::optional<std::string> argsJson = argsResolution.argsJson;
			const bool argsProvided = argsJson.has_value();
			const bool hasLegacyArgsKey = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;
			const bool shouldRequireArgsContainer =
				requestedTool == "summarize.extract" || requestedTool == "humanizer.rewrite";

			// Emit telemetry for tool invocation attempt
			{
				const std::string invokePayload =
					"{\"tool\":" + JsonString(requestedTool) +
					",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") +
					",\"hasLegacyArgsKey\":" + std::string(hasLegacyArgsKey ? "true" : "false") +
					",\"argsKey\":" + JsonString(argsResolution.selectedKey.empty() ? "none" : argsResolution.selectedKey) +
					",\"argsParseMode\":" + JsonString(argsResolution.parseMode.empty() ? "unknown" : argsResolution.parseMode) + "}";
				EmitTelemetryEvent("gateway.tool.invoke", invokePayload);
			}

			if (shouldRequireArgsContainer && !argsProvided) {
				EmitTelemetryEvent(
					"gateway.tool.args.missing",
					"{\"tool\":" + JsonString(requestedTool) +
					",\"argsKey\":" + JsonString(argsResolution.selectedKey.empty() ? "none" : argsResolution.selectedKey) +
					",\"argsParseMode\":" + JsonString(argsResolution.parseMode.empty() ? "unknown" : argsResolution.parseMode) +
					",\"softMode\":true}");
			}

			ToolExecuteResultV2 execution;
			const auto startedAt = std::chrono::steady_clock::now();
			try {
				execution = host.ExecuteRuntimeToolV2(ToolExecuteRequestV2{
					.tool = requestedTool,
					.argsJson = argsJson,
					.correlationId = request.id.empty()
						? std::string("gateway.tools.call.execute")
						: request.id,
					.deadlineEpochMs = std::nullopt,
					});
				const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - startedAt).count();
				if (elapsedMs > 30000 && !execution.executed) {
					execution.status = "tool_dispatch_timeout";
					execution.errorCode = "tool_dispatch_timeout";
					execution.errorMessage = "tool dispatch timed out before execution";
					execution.result = "{\"ok\":false,\"error\":{\"code\":\"tool_dispatch_timeout\",\"message\":\"tool dispatch timed out before execution\",\"hint\":\"verify required params and schema compatibility\"}}";
				}
			}
			catch (const std::exception& ex) {
				execution = ToolExecuteResultV2{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.result = std::string("{\"ok\":false,\"error\":{\"code\":\"tool_execute_unhandled_exception\",\"message\":\"") +
						EscapeJsonString(ex.what()) +
						"\"}}",
					.errorCode = "tool_execute_unhandled_exception",
					.errorMessage = ex.what(),
					.startedAtMs = 0,
					.completedAtMs = 0,
					.latencyMs = 0,
					.correlationId = request.id,
				};
			}
			catch (...) {
				execution = ToolExecuteResultV2{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.result = "{\"ok\":false,\"error\":{\"code\":\"tool_execute_unhandled_exception\",\"message\":\"unknown_exception\"}}",
					.errorCode = "tool_execute_unhandled_exception",
					.errorMessage = "unknown_exception",
					.startedAtMs = 0,
					.completedAtMs = 0,
					.latencyMs = 0,
					.correlationId = request.id,
				};
			}

			// Emit telemetry for tool execution result
			{
				const std::string resultPayload =
					"{\"tool\":" + JsonString(execution.tool) +
					",\"executed\":" + std::string(execution.executed ? "true" : "false") +
					",\"status\":" + JsonString(execution.status) +
					",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") +
					",\"argsKey\":" + JsonString(argsResolution.selectedKey.empty() ? "none" : argsResolution.selectedKey) +
					",\"argsParseMode\":" + JsonString(argsResolution.parseMode.empty() ? "unknown" : argsResolution.parseMode) + "}";
				EmitTelemetryEvent("gateway.tool.complete", resultPayload);
			}

			std::string payload =
				"{\"tool\":\"" + EscapeJsonString(execution.tool) +
				"\",\"executed\":" + std::string(execution.executed ? "true" : "false") +
				",\"status\":\"" + EscapeJsonString(execution.status) +
				"\",\"output\":\"" + EscapeJsonString(execution.result) +
				"\",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") +
				",\"argsKey\":\"" + EscapeJsonString(argsResolution.selectedKey.empty() ? "none" : argsResolution.selectedKey) +
				"\",\"argsParseMode\":\"" + EscapeJsonString(argsResolution.parseMode.empty() ? "unknown" : argsResolution.parseMode) + "\"";
			if (!execution.errorCode.empty()) {
				payload += ",\"errorCode\":\"" + EscapeJsonString(execution.errorCode) + "\"";
			}
			if (!execution.errorMessage.empty()) {
				payload += ",\"errorMessage\":\"" + EscapeJsonString(execution.errorMessage) + "\"";
			}
			payload += "}";

			return protocol::OkResponse(request, payload);
			});
	}

} // namespace blazeclaw::gateway::handlers::registry_introspection

namespace blazeclaw::gateway {

	void GatewayHost::RegisterGatewayRegistryIntrospectionHandlers() {
		handlers::registry_introspection::RegistryIntrospectionHandlers::RegisterAll(*this);
	}

} // namespace blazeclaw::gateway
