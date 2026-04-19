#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::config_diagnostics {

struct ConfigDiagnosticsHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::config_diagnostics

} // namespace blazeclaw::gateway
