#pragma once

namespace blazeclaw::gateway {

class GatewayHost;

namespace handlers::supplementary_catalog {

struct SupplementaryCatalogHandlers {
	static void RegisterAll(GatewayHost& host);
};

} // namespace handlers::supplementary_catalog

} // namespace blazeclaw::gateway
