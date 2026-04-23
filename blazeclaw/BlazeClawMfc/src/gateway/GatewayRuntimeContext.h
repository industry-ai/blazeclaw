#pragma once

namespace blazeclaw::gateway {

class GatewayMethodDispatcher;
class GatewayWebSocketTransport;
class GatewayAgentRegistry;
class GatewayChannelRegistry;
class GatewaySessionRegistry;
class GatewayToolRegistry;
class TransportRecipientRegistry;
class ChatRunPipelineOrchestrator;
class GatewayEventFanoutService;
class GatewayNodePairingService;
class GatewayNodeCatalogService;
class GatewayNodeCanvasCapabilityService;
class GatewayNodePendingActionQueue;
class GatewayNodeWakeService;

/// Non-owning projection of core gateway subsystems used for handler registration and routing.
/// Filled by `GatewayHost::BindRuntimeContext` before any handler that reads the context runs.
struct GatewayRuntimeContext {
	GatewayMethodDispatcher* dispatcher = nullptr;
	GatewayWebSocketTransport* transport = nullptr;
	GatewayAgentRegistry* agentRegistry = nullptr;
	GatewayChannelRegistry* channelRegistry = nullptr;
	GatewaySessionRegistry* sessionRegistry = nullptr;
	GatewayToolRegistry* toolRegistry = nullptr;
	TransportRecipientRegistry* transportRecipientRegistry = nullptr;
	ChatRunPipelineOrchestrator* chatRunPipeline = nullptr;
	GatewayEventFanoutService* eventFanout = nullptr;
	GatewayNodePairingService* nodePairing = nullptr;
	GatewayNodeCatalogService* nodeCatalog = nullptr;
	GatewayNodeCanvasCapabilityService* nodeCanvas = nullptr;
	GatewayNodePendingActionQueue* nodePending = nullptr;
	GatewayNodeWakeService* nodeWake = nullptr;

	[[nodiscard]] bool IsBound() const noexcept {
		return dispatcher != nullptr && transport != nullptr && toolRegistry != nullptr;
	}
};

} // namespace blazeclaw::gateway
