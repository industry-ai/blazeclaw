#pragma once

namespace blazeclaw::gateway::test_hooks {

/// Parity hook for OpenClaw `__resetModelCatalogCacheForTest` (`server.ts` → `server.impl.ts`).
/// Currently a documented **no-op**: `gateway.models.catalog` is still static/generated until an
/// in-process catalog cache exists; call sites can rely on the symbol for forward-compatible tests.
void ResetGatewayModelCatalogCacheForTest() noexcept;

} // namespace blazeclaw::gateway::test_hooks
