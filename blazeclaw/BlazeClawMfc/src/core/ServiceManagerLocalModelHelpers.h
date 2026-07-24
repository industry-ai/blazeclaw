#pragma once

#include "runtime/LocalModel/ITextGenerationRuntime.h"

#include <string>
#include <optional>
#include <memory>

namespace blazeclaw::core::servicemanager_localmodel {

	// Build runtime implementation from provider token while preserving existing
	// provider selection behavior.
	std::unique_ptr<localmodel::ITextGenerationRuntime> BuildRuntimeForProvider(
		const std::wstring& provider);

	// Read environment flag to decide if local model activation is forced on.
	bool ResolveLocalModelActivationFromEnv();

	// Build a stable activation reason string. If configReason is provided it is
	// returned; otherwise if envForced is true a short env-driven reason is
	// returned; otherwise an empty string is returned.
	std::string BuildLocalModelActivationReason(
		const std::optional<std::string>& configReason,
		const bool envForced);

} // namespace blazeclaw::core::servicemanager_localmodel
