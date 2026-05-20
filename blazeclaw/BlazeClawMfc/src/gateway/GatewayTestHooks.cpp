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

void SetCronSessionBusyForTest(
	GatewayHost& host,
	const std::string& sessionKey,
	const bool busy) {
	if (sessionKey.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(host.m_cronProductionMutex);
	const std::string runId = std::string("test-non-cron-busy-") + sessionKey;
	if (!busy) {
		host.m_chatRunsById.erase(runId);
		return;
	}

	GatewayHost::ChatRunState run;
	run.runId = runId;
	run.sessionKey = sessionKey;
	run.userMessage = "busy";
	run.active = true;
	host.m_chatRunsById[runId] = std::move(run);
}

} // namespace blazeclaw::gateway::test_hooks
