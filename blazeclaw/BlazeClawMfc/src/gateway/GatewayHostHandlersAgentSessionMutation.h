#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::agent_session_mutation {

struct AgentSessionMutationHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::agent_session_mutation

} // namespace blazeclaw::gateway
