#pragma once

namespace blazeclaw::core {

class ServiceManager;

/// Owns **service-layer → `GatewayHost` callback wiring** so `ServiceManager::WireGatewayCallbacks` stays a thin
/// entry point and ordering lives in one place (`WireAllGatewayServiceCallbacks`). Skills, **embedded/email policy**,
/// **embeddings**, and chat specifics are implemented here; chat execution branching lives in
/// `ChatRuntimeOrchestrationCoordinator`. **`BindToolRuntimeCallbacks`** stays on `ServiceManager` (TU-local runtime tool
/// registration helpers in `ServiceManager.cpp`) and is invoked from this coordinator — see
/// `blazeclaw/docs/SERVICE_LAYER_BOUNDARIES.md` and `blazeclaw/docs/GATEWAY_CORE_WIRING.md` (Phase C).
class GatewayHostBindingCoordinator {
public:
	/// Full startup sequence for gateway delegates: skills/schema → email/policy → tool runtimes → chat/abort → embeddings.
	static void WireAllGatewayServiceCallbacks(ServiceManager& manager);

	static void RegisterSkillsRelatedCallbacks(ServiceManager& manager);
	static void RegisterChatRuntimeCallbacks(ServiceManager& manager);

private:
	static void BindGatewayPolicyCallbacks(ServiceManager& manager);
	static void BindEmbeddingsCallbacks(ServiceManager& manager);
};

} // namespace blazeclaw::core
