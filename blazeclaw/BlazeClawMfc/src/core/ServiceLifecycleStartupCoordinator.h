#pragma once

namespace blazeclaw::config {
	struct AppConfig;
}

namespace blazeclaw::core {

class ServiceManager;

/// Owns startup policy application and module initialization sequencing for `ServiceManager::Start`,
/// keeping `ConfigurePolicies` / `InitializeModules` as thin delegation entry points.
class ServiceLifecycleStartupCoordinator {
public:
	static void ApplyConfigurePolicies(
		ServiceManager& manager,
		const blazeclaw::config::AppConfig& config);
	static void RunInitializeModules(ServiceManager& manager);
};

} // namespace blazeclaw::core
