#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::agent_tool_surface {

struct AgentToolSurfaceHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::agent_tool_surface

} // namespace blazeclaw::gateway
