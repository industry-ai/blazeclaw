#pragma once

namespace blazeclaw::core {

class ServiceManager;

/// Owns gateway host callback wiring for skills refresh/update and chat runtime registration,
/// keeping `BindSkillsCallbacks` / `BindChatCallbacks` as thin delegation entry points on `ServiceManager`.
/// Chat runtime branching (cancellation, inline tools, embedded PI, provider path, abort snapshot) is implemented
/// on `ChatRuntimeOrchestrationCoordinator` (`ChatRuntimeOrchestrationCoordinator.cpp`).
class GatewayHostBindingCoordinator {
public:
	static void RegisterSkillsRelatedCallbacks(ServiceManager& manager);
	static void RegisterChatRuntimeCallbacks(ServiceManager& manager);
};

} // namespace blazeclaw::core
