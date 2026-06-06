#include "pch.h"
#include "GatewayHost.h"
#include "GatewayTestHooks.h"

namespace blazeclaw::gateway::test_hooks {

void ResetGatewayModelCatalogCacheForTest() noexcept {
	// Intentional no-op until a mutable gateway model catalog cache is implemented.
}

void WireCronProductionIntegrationForTest(GatewayHost& host) {
	host.WireCronProductionIntegration();
}

} // namespace blazeclaw::gateway::test_hooks
