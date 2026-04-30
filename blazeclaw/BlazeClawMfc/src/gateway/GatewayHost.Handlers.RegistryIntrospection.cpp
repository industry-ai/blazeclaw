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
#include "executors/EmailScheduleExecutor.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway::handlers::registry_introspection {
	namespace {
		std::string ToLowerCopyLocal(const std::string& value) {
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

		struct EmailApproveArgsParseResult {
			bool parsed = false;
			std::string action;
			std::string approvalToken;
			bool approveRequested = false;
		};

		EmailApproveArgsParseResult ParseEmailScheduleApproveArgs(
			const std::optional<std::string>& argsJson) {
			EmailApproveArgsParseResult result;
			if (!argsJson.has_value() || json::Trim(argsJson.value()).empty()) {
				return result;
			}

			std::string action;
			if (!json::FindStringField(argsJson.value(), "action", action)) {
				return result;
			}

			result.action = json::Trim(action);
			result.parsed = true;
			json::FindStringField(argsJson.value(), "approvalToken", result.approvalToken);
			json::FindBoolField(argsJson.value(), "approve", result.approveRequested);
			result.approvalToken = json::Trim(result.approvalToken);
			return result;
		}

		struct EmailApprovalPrecheckResult {
			bool ready = true;
			std::string bucket = "ready";
			std::string errorCode;
			std::string message;
			std::string remediation;
			std::string missingDependency;
			std::string installHint;
			std::string configHint;
		};

		EmailApprovalPrecheckResult BuildEmailApprovalPrecheckResult() {
			EmailApprovalPrecheckResult result;
			const auto health = executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);
			const std::string emailSendStateLower = ToLowerCopyLocal(json::Trim(health.emailSendState));
			if (!emailSendStateLower.empty() && emailSendStateLower != "ready") {
				result.ready = false;
				result.bucket = "backend_unavailable";
				result.errorCode = "email_backend_unavailable";
				result.message = "Email delivery backend is not ready.";
				result.remediation = "Install and configure the email delivery backend, then retry approve.";
				result.missingDependency = "himalaya";
				result.installHint = "Install Himalaya CLI and ensure it is available on PATH.";
				result.configHint = "Configure a valid Himalaya account profile and SMTP/IMAP credentials.";
			}

			for (const auto& probe : health.probes) {
				if (ToLowerCopyLocal(json::Trim(probe.state)) == "ready") {
					continue;
				}

				const std::string keyLower = ToLowerCopyLocal(json::Trim(probe.key));
				if (keyLower.rfind("backend:", 0) != 0) {
					continue;
				}

				result.ready = false;
				result.bucket = "missing_skill";
				result.errorCode = "imap_smtp_skill_missing";
				result.message = probe.reasonMessage.empty()
					? "Email scheduling backend dependency is missing."
					: probe.reasonMessage;
				result.missingDependency = probe.key.size() > 8
					? probe.key.substr(8)
					: "himalaya";
				result.remediation = "Install required email scheduling backend dependencies and retry approve.";
				result.installHint = "Install Himalaya CLI and the imap_smtp_email skill runtime assets.";
				result.configHint = "Verify backend binaries, skill scripts, and account configuration.";
				break;
			}

			return result;
		}

		std::string BuildEmailApprovalReadinessJson() {
			const auto precheck = BuildEmailApprovalPrecheckResult();
			return std::string("{\"tool\":\"email.schedule\",\"ready\":") +
				std::string(precheck.ready ? "true" : "false") +
				",\"bucket\":" + JsonString(precheck.bucket) +
				",\"errorCode\":" + JsonString(precheck.errorCode.empty() ? "none" : precheck.errorCode) +
				",\"message\":" + JsonString(precheck.message) +
				",\"remediation\":" + JsonString(precheck.remediation) +
				",\"missingDependency\":" + JsonString(precheck.missingDependency) +
				",\"installHint\":" + JsonString(precheck.installHint) +
				",\"configHint\":" + JsonString(precheck.configHint) +
				"}";
		}

		std::string BuildRuntimeFreshnessJson() {
			const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			const std::string buildStamp = std::string(__DATE__) + " " + std::string(__TIME__);
			return std::string("{\"gatewayBuildStamp\":") + JsonString(buildStamp) +
				",\"webAssetsStamp\":" + JsonString(buildStamp) +
				",\"generatedAtEpochMs\":" + std::to_string(now) +
				"}";
		}

		std::string BuildApprovalFailureResultJson(
			const std::string& code,
			const std::string& message,
			const std::string& remediation,
			const std::string& missingDependency,
			const std::string& installHint,
			const std::string& configHint,
			const std::string& bucket) {
			return std::string("{\"ok\":false,\"error\":{\"code\":") +
				JsonString(code.empty() ? "email_approval_failed" : code) +
				",\"message\":" +
				JsonString(message.empty() ? "email approval execution failed" : message) +
				",\"category\":" +
				JsonString("approval_execution") +
				",\"bucket\":" +
				JsonString(bucket.empty() ? "unknown" : bucket) +
				",\"remediation\":" +
				JsonString(remediation) +
				",\"missingDependency\":" +
				JsonString(missingDependency) +
				",\"installHint\":" +
				JsonString(installHint) +
				",\"configHint\":" +
				JsonString(configHint) +
				"}}";
		}

		std::string ResolveApprovalFailureCode(
			const ToolExecuteResultV2& execution,
			const std::string& lowerResult,
			std::string& outBucket,
			std::string& outMessage,
			std::string& outRemediation,
			std::string& outMissingDependency,
			std::string& outInstallHint,
			std::string& outConfigHint) {
			const std::string lowerErrorCode = ToLowerCopyLocal(json::Trim(execution.errorCode));
			if (lowerErrorCode == "approval_token_expired" ||
				lowerResult.find("approval_token_expired") != std::string::npos ||
				lowerResult.find("token_expired") != std::string::npos) {
				outBucket = "token_expired";
				outMessage = "Approval token expired. Request a new approval token and retry.";
				outRemediation = "Generate a fresh approval token, then re-run approve action.";
				outMissingDependency.clear();
				outInstallHint.clear();
				outConfigHint = "Retry approval promptly before token expiration.";
				return "approval_token_expired";
			}

			if (lowerErrorCode == "imap_smtp_skill_missing" ||
				lowerResult.find("imap_smtp_skill_missing") != std::string::npos ||
				lowerResult.find("email_delivery_backends_exhausted") != std::string::npos) {
				outBucket = "missing_skill";
				outMessage = "Email backend dependency is missing for approval execution.";
				outRemediation = "Install required email skill backend dependencies and retry approve.";
				outMissingDependency = "imap_smtp_email";
				outInstallHint = "Install Himalaya CLI and imap_smtp_email skill assets.";
				outConfigHint = "Verify backend scripts and account profile configuration.";
				return "imap_smtp_skill_missing";
			}

			if (lowerErrorCode == "email_backend_unavailable" ||
				lowerResult.find("backend_unavailable") != std::string::npos ||
				lowerResult.find("himalaya") != std::string::npos) {
				outBucket = "backend_unavailable";
				outMessage = "Email delivery backend is unavailable for approval execution.";
				outRemediation = "Restore backend runtime availability and retry approve.";
				outMissingDependency = "himalaya";
				outInstallHint = "Install Himalaya CLI and add it to PATH.";
				outConfigHint = "Configure Himalaya account and SMTP/IMAP credentials.";
				return "email_backend_unavailable";
			}

			outBucket = "approval_execution_failed";
			outMessage = execution.errorMessage.empty()
				? "Email approval execution failed."
				: execution.errorMessage;
			outRemediation = "Inspect email.schedule tool output and retry after fixing backend/runtime conditions.";
			outMissingDependency.clear();
			outInstallHint.clear();
			outConfigHint.clear();
			return execution.errorCode.empty()
				? "email_approval_failed"
				: execution.errorCode;
		}
	}

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

		host.m_dispatcher.Register("gateway.email.backend.readiness", [](const protocol::RequestFrame& request) {
			const std::string payload = BuildEmailApprovalReadinessJson();
			EmitTelemetryEvent("gateway.email.backend.readiness", payload);
			return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register("gateway.runtime.freshness", [](const protocol::RequestFrame& request) {
			const std::string payload = BuildRuntimeFreshnessJson();
			EmitTelemetryEvent("gateway.runtime.freshness", payload);
			return protocol::OkResponse(request, payload);
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
			const auto approveArgs = ParseEmailScheduleApproveArgs(argsJson);
			const std::string approvalToken = approveArgs.approvalToken;
			const bool approveRequested = approveArgs.approveRequested;
			const bool approveActionMatched =
				ToLowerCopyLocal(approveArgs.action) == "approve";
			const bool isEmailScheduleApproveRequest =
				requestedTool == "email.schedule" &&
				approveArgs.parsed &&
				approveActionMatched;
			EmitTelemetryEvent(
				"gateway.email.approval.remap.gate",
				std::string("{\"tool\":") + JsonString(requestedTool) +
				",\"action\":" + JsonString(approveArgs.action.empty() ? "(missing)" : approveArgs.action) +
				",\"approve\":" + std::string(approveRequested ? "true" : "false") +
				",\"parsed\":" + std::string(approveArgs.parsed ? "true" : "false") +
				",\"gateEntered\":" + std::string(isEmailScheduleApproveRequest ? "true" : "false") +
				",\"argsKey\":" + JsonString(argsResolution.selectedKey.empty() ? "none" : argsResolution.selectedKey) +
				",\"argsParseMode\":" + JsonString(argsResolution.parseMode.empty() ? "unknown" : argsResolution.parseMode) +
				"}");
			if (isEmailScheduleApproveRequest && approveRequested) {
				const auto precheck = BuildEmailApprovalPrecheckResult();
				if (!precheck.ready) {
					execution = ToolExecuteResultV2{
						.tool = requestedTool,
						.executed = false,
						.status = "error",
						.result = BuildApprovalFailureResultJson(
							precheck.errorCode,
							precheck.message,
							precheck.remediation,
							precheck.missingDependency,
							precheck.installHint,
							precheck.configHint,
							precheck.bucket),
						.errorCode = precheck.errorCode,
						.errorMessage = precheck.message,
						.startedAtMs = 0,
						.completedAtMs = 0,
						.latencyMs = 0,
						.correlationId = request.id,
					};
					EmitTelemetryEvent(
						"gateway.email.approval.execute.failure",
						std::string("{\"tool\":") + JsonString(requestedTool) +
						",\"bucket\":" + JsonString(precheck.bucket) +
						",\"code\":" + JsonString(precheck.errorCode) +
						",\"phase\":" + JsonString("precheck") +
						",\"approvalTokenPresent\":" +
						std::string(approvalToken.empty() ? "false" : "true") +
						"}");
				}
			}

			if (execution.tool.empty()) {
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
			}

			if (isEmailScheduleApproveRequest) {
				const std::string lowerResult = ToLowerCopyLocal(execution.result);
				const bool shouldNormalizeApproval =
					execution.status == "error" ||
					execution.status == "failed" ||
					!execution.errorCode.empty() ||
					lowerResult.find("\"error\"") != std::string::npos ||
					lowerResult.find("imap_smtp_skill_missing") != std::string::npos ||
					lowerResult.find("legacy_execution_failed") != std::string::npos;
				if (shouldNormalizeApproval) {
					std::string bucket;
					std::string mappedMessage;
					std::string remediation;
					std::string missingDependency;
					std::string installHint;
					std::string configHint;
					const std::string mappedCode = ResolveApprovalFailureCode(
						execution,
						lowerResult,
						bucket,
						mappedMessage,
						remediation,
						missingDependency,
						installHint,
						configHint);
					execution.status = "error";
					execution.errorCode = mappedCode;
					execution.errorMessage = mappedMessage;
					execution.result = BuildApprovalFailureResultJson(
						mappedCode,
						mappedMessage,
						remediation,
						missingDependency,
						installHint,
						configHint,
						bucket);
					EmitTelemetryEvent(
						"gateway.email.approval.execute.failure",
						std::string("{\"tool\":") + JsonString(requestedTool) +
						",\"bucket\":" + JsonString(bucket) +
						",\"code\":" + JsonString(mappedCode) +
						",\"phase\":" + JsonString("execution") +
						",\"approvalTokenPresent\":" +
						std::string(approvalToken.empty() ? "false" : "true") +
						"}");
					EmitTelemetryEvent(
						"gateway.email.approval.remap.applied",
						std::string("{\"tool\":") + JsonString(requestedTool) +
						",\"mappedCode\":" + JsonString(mappedCode) +
						",\"bucket\":" + JsonString(bucket) +
						",\"hasRemediation\":" + std::string(remediation.empty() ? "false" : "true") +
						",\"hasMissingDependency\":" + std::string(missingDependency.empty() ? "false" : "true") +
						",\"hasInstallHint\":" + std::string(installHint.empty() ? "false" : "true") +
						",\"hasConfigHint\":" + std::string(configHint.empty() ? "false" : "true") +
						"}");
				}
			}

			// Emit telemetry for tool execution result
			{
				const std::string resultPayload =
					"{\"tool\":" + JsonString(execution.tool) +
					",\"executed\":" + std::string(execution.executed ? "true" : "false") +
					",\"status\":" + JsonString(execution.status) +
					",\"errorCode\":" + JsonString(execution.errorCode.empty() ? "none" : execution.errorCode) +
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
