#include "pch.h"
#include "ServiceManagerLifecycleHelpers.h"

#include "ServiceManagerTextHelpers.h"

#ifndef BLAZECLAW_TESTS_NO_RUNTIME_DEPENDENCIES
#include "bootstrap/CServiceBootstrapCoordinator.h"
#endif

namespace blazeclaw::core::servicemanager_lifecycle {

	void AppendStartupTrace(const char* stage) {
	#ifdef BLAZECLAW_TESTS_NO_RUNTIME_DEPENDENCIES
		(void)stage;
	#else
		CServiceBootstrapCoordinator coordinator;
		coordinator.AppendStartupTrace(stage);
	#endif
	}

	bool SuppressStartupMigrationsFromEnv() {
		return blazeclaw::core::servicemanager_text::SuppressStartupMigrationsFromEnv();
	}

} // namespace blazeclaw::core::servicemanager_lifecycle
