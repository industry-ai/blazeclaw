#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::registry_introspection {

struct RegistryIntrospectionHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::registry_introspection

} // namespace blazeclaw::gateway
