#pragma once

#include <string>

namespace blazeclaw::core::servicemanager_lifecycle {

	// Append a short startup stage marker to the bootstrap coordinator.
	void AppendStartupTrace(const char* stage);

	// Read environment to decide whether to suppress startup migrations.
	bool SuppressStartupMigrationsFromEnv();

} // namespace blazeclaw::core::servicemanager_lifecycle
