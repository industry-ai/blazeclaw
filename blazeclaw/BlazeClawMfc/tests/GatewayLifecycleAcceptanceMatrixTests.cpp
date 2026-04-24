#include <catch2/catch_all.hpp>

#include "config/ConfigModels.h"
#include "core/bootstrap/GatewayRuntimeBootstrapCoordinator.h"
#include "core/diagnostics/CDiagnosticsReportBuilder.h"
#include "core/diagnostics/DiagnosticsSnapshot.h"
#include "gateway/GatewayHost.h"
#include "gateway/GatewayMethodSurfaceAudit.h"

#include <string>

namespace {

	void VerifyExecuteStartupAnchors() {
		blazeclaw::config::AppConfig config;
		config.gateway.startupMode = L"local_runtime_dispatch";
		blazeclaw::gateway::GatewayHost host;
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator coordinator;
		std::vector<std::string> traces;
		const auto startupResult = coordinator.ExecuteStartup(
			blazeclaw::core::GatewayRuntimeBootstrapCoordinator::StartupContext{
				.config = config,
				.gatewayHost = host,
				.appendTrace = [&traces](const char* stage) {
					traces.emplace_back(stage == nullptr ? "" : stage);
				},
			});
		REQUIRE(startupResult.success);
		REQUIRE_FALSE(traces.empty());
		REQUIRE(traces.front() == "GatewayRuntimeBootstrap.ExecuteStartup.begin");
		REQUIRE(traces.back() == "GatewayRuntimeBootstrap.ExecuteStartup.success");
	}

	void VerifyResolveRuntimeConfig() {
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator coordinator;
		blazeclaw::config::AppConfig config;
		config.gateway.bindAddress = L"127.0.0.1";
		config.gateway.port = 18080;
		const auto r = coordinator.ResolveGatewayRuntimeConfig(config);
		REQUIRE_FALSE(r.bindAddressUtf8.empty());
		REQUIRE(r.port == 18080);
	}

	void VerifyParityContractSchemaV5() {
		REQUIRE(blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion == 6);
	}

	void VerifyParityJsonShutdownAndS4Fields() {
		blazeclaw::core::GatewayParityLifecycleContract c;
		c.schemaVersion = blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion;
		c.extensionSurfaceReloadCount = 1;
		c.lastAppliedExtensionSurfaceEpoch = 2;
		c.lastExtensionSurfaceMethodDeltaJson = R"({"ok":true})";
		c.gatewayShutdownInvocationCount = 1;
		c.lastShutdownPrelude.recordedAtEpochMs = 99;
		c.lastShutdownPrelude.snapshotCleanupPathUtf8 = "normal_stop";
		c.lastShutdownPrelude.phaseOrderUtf8 = { "non_gateway_runtime_cleanup" };
		c.lastShutdownPrelude.pluginGlobalStopInvoked = true;

		blazeclaw::core::CDiagnosticsReportBuilder builder;
		const std::string json = builder.SerializeParityLifecycleContractJson(c);
		REQUIRE(json.find("\"schemaVersion\":6") != std::string::npos);
		REQUIRE(json.find("\"gatewayShutdownInvocationCount\":1") != std::string::npos);
		REQUIRE(json.find("\"lastShutdownPrelude\":{") != std::string::npos);
		REQUIRE(json.find("\"pluginGlobalStopInvoked\":true") != std::string::npos);
		REQUIRE(json.find("\"extensionSurfaceReloadCount\":1") != std::string::npos);
	}

	void VerifyChannelSurfaceAuditBaseline() {
		REQUIRE(blazeclaw::gateway::GatewayChannelHandlerSurfaceMethodNames().size() == 35);
	}

	void VerifyShutdownOwnedCleanupOrder() {
		const auto& order =
			blazeclaw::core::GatewayRuntimeBootstrapCoordinator::NormalStopGatewayOwnedCleanupExecutionOrder();
		REQUIRE(order.size() == 5);
		REQUIRE(order[0] == "plugin_global_stop");
		REQUIRE(order[1] == "gateway_host_stop");
	}

	void VerifyDeferredExtensionReloadSurface() {
		blazeclaw::gateway::GatewayHost host;
		blazeclaw::config::GatewayConfig gatewayConfig;
		gatewayConfig.bindAddress = L"127.0.0.1";
		gatewayConfig.port = 18789;
		REQUIRE(host.StartLocalOnly(gatewayConfig));
		std::string deltaJson;
		REQUIRE(host.PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry(deltaJson));
		REQUIRE(deltaJson.find("\"ok\":true") != std::string::npos);
		host.Stop();
	}

	struct AcceptanceRow {
		const char* openclawLifecycleBlock;
		const char* blazeclawEvidence;
		void (*verify)();
	};

} // namespace

TEST_CASE(
	"S6 acceptance: OpenClaw server.impl.ts lifecycle parity matrix (evidence hooks)",
	"[parity][s6][acceptance][lifecycle-parity-ci]")
{
	static const AcceptanceRow kRows[] = {
		{ "bootstrap / ExecuteStartup",
			"GatewayRuntimeBootstrapCoordinator::ExecuteStartup + trace anchors",
			&VerifyExecuteStartupAnchors },
		{ "resolveGatewayRuntimeConfig",
			"GatewayRuntimeBootstrapCoordinator::ResolveGatewayRuntimeConfig",
			&VerifyResolveRuntimeConfig },
		{ "parity lifecycle export (operator / RPC)",
			"GatewayParityLifecycleContract schema v5",
			&VerifyParityContractSchemaV5 },
		{ "parity JSON serialization",
			"CDiagnosticsReportBuilder::SerializeParityLifecycleContractJson (S4+S5 fields)",
			&VerifyParityJsonShutdownAndS4Fields },
		{ "channel RPC surface audit",
			"GatewayMethodSurfaceAudit channel list (35)",
			&VerifyChannelSurfaceAuditBaseline },
		{ "shutdown owned-cleanup LIFO contract",
			"GatewayRuntimeBootstrapCoordinator::NormalStopGatewayOwnedCleanupExecutionOrder",
			&VerifyShutdownOwnedCleanupOrder },
		{ "deferred extension catalog + method surface",
			"GatewayHost::PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry",
			&VerifyDeferredExtensionReloadSurface },
	};

	for (const auto& row : kRows) {
		INFO("OpenClaw block: " << row.openclawLifecycleBlock << "; BlazeClaw: " << row.blazeclawEvidence);
		REQUIRE(row.verify != nullptr);
		row.verify();
	}
}
