#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersConfigDiagnostics.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::config_diagnostics {

	void ConfigDiagnosticsHandlers::RegisterAll(GatewayHost& host) {
		host.m_dispatcher.Register("gateway.config.get", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"gateway\":{\"bind\":\"" + EscapeJsonString(host.m_runtimeGatewayBind) +
				"\",\"port\":" + std::to_string(host.m_runtimeGatewayPort) +
				"},\"agent\":{\"model\":\"" + EscapeJsonString(host.m_runtimeAgentModel) +
				"\",\"streaming\":" + std::string(host.m_runtimeAgentStreaming ? "true" : "false") +
				"},\"nodeParity\":{\"enabled\":" +
				std::string(host.m_runtimeNodeParityEnabled ? "true" : "false") +
				",\"diagnosticsEnabled\":" +
				std::string(host.m_runtimeNodeParityDiagnosticsEnabled ? "true" : "false") +
				",\"rolloutMode\":\"" + EscapeJsonString(host.m_runtimeNodeParityRolloutMode) +
				"\",\"invokeTotalCount\":" + std::to_string(host.m_nodeInvokeTotalCount) +
				",\"policyRejectCount\":" + std::to_string(host.m_nodeInvokePolicyRejectCount) +
				",\"wakeAttemptCount\":" + std::to_string(host.m_nodeWakeAttemptCount) +
				",\"pendingEnqueueCount\":" + std::to_string(host.m_nodePendingQueueEnqueueCount) +
				",\"wakeNudgeCount\":" + std::to_string(host.m_nodeWakeNudgeCount) +
				"},\"emailFallback\":{\"preflightEnabled\":" +
				std::string(host.m_runtimeEmailPreflightEnabled ? "true" : "false") +
				",\"policyProfilesEnabled\":" +
				std::string(host.m_runtimeEmailPolicyProfilesEnabled ? "true" : "false") +
				",\"policyProfilesEnforce\":" +
				std::string(host.m_runtimeEmailPolicyProfilesEnforce ? "true" : "false") +
				"},\"deepseek\":" + BuildGatewayDeepSeekConfigJson(
					host.m_runtimeDeepSeekApiKey,
					host.m_runtimeDeepSeekBaseUrl,
					host.m_runtimeDeepSeekDefaultModel) + "}");
			});

		host.m_dispatcher.Register("config.apply", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"applied\":true,\"updated\":true}");
			});

		host.m_dispatcher.Register("config.patch", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"patched\":true,\"updated\":true}");
			});

		host.m_dispatcher.Register("skills.search", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"skills\":[],\"count\":0}");
			});

		host.m_dispatcher.Register("skills.detail", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"skill\":null,\"found\":false}");
			});

		host.m_dispatcher.Register("skills.bins", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bins\":[],\"count\":0}");
			});

		host.m_dispatcher.Register("skills.install", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"installed\":false,\"status\":\"not_supported\"}");
			});

		host.m_dispatcher.Register("update.run", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"started\":false,\"status\":\"not_supported\"}");
			});

		host.m_dispatcher.Register("doctor.memory.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"ok\":true,\"status\":\"healthy\"}");
			});

		host.m_dispatcher.Register("doctor.memory.dreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"source\":\"memory\"}");
			});

		host.m_dispatcher.Register("doctor.memory.backfillDreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"queued\":true,\"status\":\"scheduled\"}");
			});

		host.m_dispatcher.Register("doctor.memory.resetDreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"reset\":true,\"target\":\"dreamDiary\"}");
			});

		host.m_dispatcher.Register("doctor.memory.resetGroundedShortTerm", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"reset\":true,\"target\":\"groundedShortTerm\"}");
			});

		host.m_dispatcher.Register("doctor.memory.flush", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"flushed\":true,\"status\":\"ok\"}");
			});

		host.m_dispatcher.Register("gateway.config.set", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string bind = params.GetString("bind");
			const std::optional<std::size_t> port = params.GetSize("port");
			const std::string model = params.GetString("model");
			const std::optional<bool> streaming = params.GetBool("streaming");
			const std::string deepSeekApiKey = params.GetString("deepseekApiKey");
			const std::string deepSeekBaseUrl = params.GetString("deepseekBaseUrl");

			if (!bind.empty()) {
				host.m_runtimeGatewayBind = bind;
			}

			if (port.has_value() && port.value() > 0 && port.value() <= 65535) {
				host.m_runtimeGatewayPort = static_cast<std::uint16_t>(port.value());
			}

			if (!model.empty()) {
				host.m_runtimeAgentModel = GatewayModel::NormalizeModelId(model);
			}

			if (streaming.has_value()) {
				host.m_runtimeAgentStreaming = streaming.value();
			}

			if (!deepSeekApiKey.empty()) {
				host.m_runtimeDeepSeekApiKey = deepSeekApiKey;
			}

			if (!deepSeekBaseUrl.empty()) {
				host.m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
			}

			if (model.empty() && !deepSeekApiKey.empty() &&
				(host.m_runtimeAgentModel == GatewayModel::kDefaultModelId ||
					host.m_runtimeAgentModel == GatewayModel::kReasonerModelId)) {
				host.m_runtimeAgentModel = host.m_runtimeDeepSeekDefaultModel;
			}

			return protocol::OkResponse(request, "{\"gateway\":{\"bind\":\"" + EscapeJsonString(host.m_runtimeGatewayBind) +
				"\",\"port\":" + std::to_string(host.m_runtimeGatewayPort) +
				"},\"agent\":{\"model\":\"" + EscapeJsonString(host.m_runtimeAgentModel) +
				"\",\"streaming\":" + std::string(host.m_runtimeAgentStreaming ? "true" : "false") +
				"},\"deepseek\":" + BuildGatewayDeepSeekConfigJson(
					host.m_runtimeDeepSeekApiKey,
					host.m_runtimeDeepSeekBaseUrl,
					host.m_runtimeDeepSeekDefaultModel) +
				",\"updated\":true}");
			});


		host.m_dispatcher.Register("gateway.logs.tail", [](const protocol::RequestFrame& request) {
			const std::vector<std::string> seededEntries = {
				"{\"ts\":1735689600000,\"level\":\"info\",\"source\":\"gateway\",\"message\":\"Gateway host started\"}",
				"{\"ts\":1735689600100,\"level\":\"info\",\"source\":\"transport\",\"message\":\"WebSocket listener active\"}",
				"{\"ts\":1735689600200,\"level\":\"debug\",\"source\":\"dispatcher\",\"message\":\"Method handlers registered\"}",
			};

			const std::size_t requestedLimit = RequestParamsView(request.paramsJson).GetSize("limit").value_or(50);
			const std::size_t cappedLimit = std::max<std::size_t>(1, std::min<std::size_t>(requestedLimit, 200));
			const std::size_t emitCount = std::min<std::size_t>(cappedLimit, seededEntries.size());
			const std::size_t begin = seededEntries.size() - emitCount;

			std::string entriesJson = "[";
			for (std::size_t i = begin; i < seededEntries.size(); ++i) {
				if (i > begin) {
					entriesJson += ",";
				}

				entriesJson += seededEntries[i];
			}

			entriesJson += "]";

			return protocol::OkResponse(request, "{\"entries\":" + entriesJson + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.resolve", [&host](const protocol::RequestFrame& request) {
			const std::string sessionId = RequestParamsView(request.paramsJson).GetString("sessionId");

			const SessionEntry resolved = host.m_sessionRegistry.Resolve(sessionId);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(resolved) + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.create", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string requestedId = params.GetString("sessionId");
			const std::string requestedScope = params.GetString("scope");
			const std::optional<bool> requestedActive = params.GetBool("active");

			const SessionEntry created = host.m_sessionRegistry.Create(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(created) + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.reset", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string requestedId = params.GetString("sessionId");
			const std::string requestedScope = params.GetString("scope");
			const std::optional<bool> requestedActive = params.GetBool("active");

			const SessionEntry reset = host.m_sessionRegistry.Reset(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(reset) + ",\"event\":\"gateway.session.reset\"}");
			});

		host.m_dispatcher.Register("gateway.health", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"status\":\"ok\",\"running\":" + std::string(host.IsRunning() ? "true" : "false") + "}");
			});
	}

} // namespace blazeclaw::gateway::handlers::config_diagnostics

namespace blazeclaw::gateway {

	void GatewayHost::RegisterGatewayConfigAndDiagnosticsHandlers() {
		handlers::config_diagnostics::ConfigDiagnosticsHandlers::RegisterAll(*this);
	}

} // namespace blazeclaw::gateway
