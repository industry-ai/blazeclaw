#include "pch.h"
#include "ServiceManagerLifecycleHelpers.h"

#include "bootstrap/CServiceBootstrapCoordinator.h"
#include "ServiceManagerTextHelpers.h"

namespace blazeclaw::core::servicemanager_lifecycle {

	void AppendStartupTrace(const char* stage) {
		CServiceBootstrapCoordinator coordinator;
		coordinator.AppendStartupTrace(stage);
	}

	bool SuppressStartupMigrationsFromEnv() {
		return blazeclaw::core::servicemanager_text::SuppressStartupMigrationsFromEnv();
	}

} // namespace blazeclaw::core::servicemanager_lifecycle
