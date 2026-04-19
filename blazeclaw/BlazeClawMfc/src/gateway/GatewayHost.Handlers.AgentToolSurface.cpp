#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersAgentToolSurface.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>

namespace blazeclaw::gateway::handlers::agent_tool_surface {

void AgentToolSurfaceHandlers::RegisterAll(GatewayHost& host) {
host.m_dispatcher.Register("gateway.agents.get", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const AgentEntry agent = host.m_agentRegistry.Get(requestedId);
			return protocol::OkResponse(request, "{\"agent\":" + SerializeAgent(agent) + "}");
			});

		host.m_dispatcher.Register("gateway.tools.catalog", [&host](const protocol::RequestFrame& request) {
			const auto tools = host.m_toolRegistry.List();
			std::string toolsJson = "[";
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (i > 0) {
					toolsJson += ",";
				}

				toolsJson += SerializeTool(tools[i]);
			}

			toolsJson += "]";

			return protocol::OkResponse(request, "{\"tools\":" + toolsJson + "}");
			});

		host.m_dispatcher.Register("gateway.tools.call.preview", [&host](const protocol::RequestFrame& request) {
			const std::string requestedTool = RequestParamsView(request.paramsJson).GetString("tool");
			const ToolPreviewResult preview = host.m_toolRegistry.Preview(requestedTool);
			const bool argsProvided = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;

			return protocol::OkResponse(request, "{\"tool\":\"" + EscapeJsonString(preview.tool) + "\",\"allowed\":" +
					std::string(preview.allowed ? "true" : "false") + ",\"reason\":\"" +
					EscapeJsonString(preview.reason) + "\",\"argsProvided\":" +
					std::string(argsProvided ? "true" : "false") +
				   ",\"policy\":\"dynamic_runtime_preview_v1\"}");
			});

		host.m_dispatcher.Register("gateway.agents.activate", [&host](const protocol::RequestFrame& request) {
			const std::string requestedId = RequestParamsView(request.paramsJson).GetString("agentId");
			const AgentEntry agent = host.m_agentRegistry.Activate(requestedId);
			return protocol::OkResponse(request, "{\"agent\":" + SerializeAgent(agent) + ",\"event\":\"gateway.agent.update\"}");
			});
}

} // namespace blazeclaw::gateway::handlers::agent_tool_surface

namespace blazeclaw::gateway {

void GatewayHost::RegisterGatewayAgentToolSurfaceHandlers() {
	handlers::agent_tool_surface::AgentToolSurfaceHandlers::RegisterAll(*this);
}

} // namespace blazeclaw::gateway
