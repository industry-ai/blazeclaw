#pragma once

#include <optional>
#include <string>

namespace blazeclaw::cron {

	// Phase DI Step 5: product-gated native cron-cli parity shim.
	// WebView `/cron` remains the active parity surface; this maps OpenClaw-style
	// command verbs to gateway RPC methods for future native MFC/CLI integration.
	struct CronCliGatewayRoute {
		std::string command;
		std::string gatewayMethod;
		bool mutating = false;
	};

	std::optional<CronCliGatewayRoute> MapCronCliCommandToGatewayRoute(
		const std::string& command);

} // namespace blazeclaw::cron
