#pragma once

#include <functional>
#include <string>
#include <vector>

namespace blazeclaw::core::startup {

struct ServiceStartupPhaseModule {
	std::string id;
	std::function<void()> execute;
};

class ServiceStartupPhaseModuleRunner {
public:
	static void Execute(const ServiceStartupPhaseModule& module);
	static void ExecuteAll(const std::vector<ServiceStartupPhaseModule>& modules);
};

} // namespace blazeclaw::core::startup
