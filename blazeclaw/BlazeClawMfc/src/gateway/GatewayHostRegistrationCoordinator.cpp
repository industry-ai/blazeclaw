#include "pch.h"
#include "GatewayHostRegistrationCoordinator.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway::GatewayHostRegistration {

void RegisterDefaultHandlerSequence(GatewayHost& host) {
	// Domain: channels + event tables (`GatewayHost.Handlers.Channels` / `.Events`).
	host.RegisterChannelsHandlers();
	host.RegisterEventHandlers();

	// Domain: tool execution history, tools surface, generated scope-cluster catalog.
	host.RegisterToolExecutionHistoryHandlers();
	host.RegisterToolsHandlers();
	host.RegisterGeneratedScopeClusterHandlers();

	// Domain: gateway event queries + registry introspection.
	host.RegisterGatewayEventCatalogQueryHandlers();
	host.RegisterGatewayRegistryIntrospectionHandlers();

	// Domain: agent/session mutations and agent/tool surface.
	host.RegisterGatewayAgentSessionMutationHandlers();
	host.RegisterGatewayAgentToolSurfaceHandlers();

	// Domain: config, diagnostics, security ops.
	host.RegisterGatewayConfigAndDiagnosticsHandlers();
	host.RegisterSecurityOpsHandlers();

	// Domain: chat/runtime/task pipeline + transport.
	host.RegisterRuntimeHandlers();
	host.RegisterTransportHandlers();

	// Last: supplementary catalog-style methods (ordering-sensitive vs transport).
	host.RegisterGatewaySupplementaryCatalogHandlers();
}

} // namespace blazeclaw::gateway::GatewayHostRegistration
