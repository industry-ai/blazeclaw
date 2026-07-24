#pragma once

#include "../config/ConfigModels.h"

namespace blazeclaw::core::servicemanager_snapshot {

	// Lightweight gateway-facing status snapshot produced by the helper to
	// avoid depending on gateway types directly during the extraction.
	struct GatewayStatusSnapshot {
		bool supported = true;
		bool ready = false;
		std::string status;
		std::uint64_t uptimeMs = 0;
	};

	// Collect a lightweight gateway-facing status snapshot. Non-throwing.
	GatewayStatusSnapshot CollectGatewayStatusSnapshot(const blazeclaw::config::AppConfig& config);

	// Collect an aggregated runtime health snapshot for telemetry/UI.
	struct RuntimeHealthSnapshot {
		bool allSystemsGo = true;
		std::string summary;
	};

	RuntimeHealthSnapshot CollectRuntimeHealthSnapshot();

} // namespace blazeclaw::core::servicemanager_snapshot
