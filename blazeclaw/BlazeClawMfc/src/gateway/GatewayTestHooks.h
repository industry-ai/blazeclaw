#pragma once

#include <string>

namespace blazeclaw::gateway {
	class GatewayHost;
}

namespace blazeclaw::gateway::test_hooks {

/// Parity hook for OpenClaw `__resetModelCatalogCacheForTest` (`server.ts` → `server.impl.ts`).
/// Currently a documented **no-op**: `gateway.models.catalog` is still static/generated until an
/// in-process catalog cache exists; call sites can rely on the symbol for forward-compatible tests.
void ResetGatewayModelCatalogCacheForTest() noexcept;

/// Wires cron production runtime adapters and task-ledger hooks on a local-dispatch gateway host.
void WireCronProductionIntegrationForTest(GatewayHost& host);

/// Injects an active non-cron chat run for deterministic cron busy-lane tests.
void SetCronSessionBusyForTest(
	GatewayHost& host,
	const std::string& sessionKey,
	bool busy);

} // namespace blazeclaw::gateway::test_hooks
