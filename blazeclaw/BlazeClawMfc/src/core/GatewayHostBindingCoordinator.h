#pragma once

namespace blazeclaw::core {

class ServiceManager;

/// Owns **service-layer → `GatewayHost` callback wiring** so `ServiceManager::WireGatewayCallbacks` stays a thin
/// entry point and ordering lives in one place (`WireAllGatewayServiceCallbacks`). Skills/chat specifics stay here;
/// chat execution branching lives in `ChatRuntimeOrchestrationCoordinator`. Policy/tool/embeddings bindings remain on
/// `ServiceManager` as `Bind*` helpers; the coordinator sequences them — see `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md`.
class GatewayHostBindingCoordinator {
public:
	/// Full startup sequence for gateway delegates: skills/schema → email/policy → tool runtimes → chat/abort → embeddings.
	static void WireAllGatewayServiceCallbacks(ServiceManager& manager);

	static void RegisterSkillsRelatedCallbacks(ServiceManager& manager);
	static void RegisterChatRuntimeCallbacks(ServiceManager& manager);
};

} // namespace blazeclaw::core
