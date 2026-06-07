#include "pch.h"
#include "ServiceStartupPhaseModule.h"

#include <stdexcept>

namespace blazeclaw::core::startup {

void ServiceStartupPhaseModuleRunner::Execute(
	const ServiceStartupPhaseModule& module) {
	if (!module.execute) {
		throw std::invalid_argument("startup phase module execute callback is required");
	}

	module.execute();
}

void ServiceStartupPhaseModuleRunner::ExecuteAll(
	const std::vector<ServiceStartupPhaseModule>& modules) {
	for (const auto& module : modules) {
		Execute(module);
	}
}

} // namespace blazeclaw::core::startup
