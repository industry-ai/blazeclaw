#pragma once

namespace blazeclaw::core {

class ServiceManager;

/// Owns gateway host callback wiring for skills refresh/update and chat runtime paths,
/// keeping `BindSkillsCallbacks` / `BindChatCallbacks` as thin delegation entry points on `ServiceManager`.
class GatewayHostBindingCoordinator {
public:
	static void RegisterSkillsRelatedCallbacks(ServiceManager& manager);
	static void RegisterChatRuntimeCallbacks(ServiceManager& manager);
};

} // namespace blazeclaw::core
