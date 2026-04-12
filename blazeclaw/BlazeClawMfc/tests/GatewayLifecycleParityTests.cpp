#include <catch2/catch_all.hpp>

#include "core/bootstrap/GatewayManagedConfigReloader.h"
#include "core/bootstrap/GatewayRuntimeBootstrapCoordinator.h"
#include "core/diagnostics/CDiagnosticsReportBuilder.h"
#include "gateway/TransportRecipientRegistry.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

	std::filesystem::path CreateTempConfigPath(const std::wstring& suffix)
	{
		const auto tempRoot = std::filesystem::temp_directory_path();
		const std::wstring filename = L"blazeclaw_gateway_reloader_" +
			suffix +
			L".conf";
		return tempRoot / filename;
	}

	void WriteConfigFile(
		const std::filesystem::path& path,
		const std::string& content)
	{
		std::ofstream out(path, std::ios::out | std::ios::trunc);
		REQUIRE(out.is_open());
		out << content;
		out.flush();
	}

	std::uint64_t ComputeFileContentHash(
		const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::in | std::ios::binary);
		REQUIRE(in.is_open());

		const std::string content(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
		return std::hash<std::string>{}(content);
	}

} // namespace

TEST_CASE(
	"GatewayRuntimeBootstrapCoordinator close prelude emits deterministic trace order",
	"[gateway][lifecycle][p2]")
{
	blazeclaw::gateway::GatewayHost host;
	blazeclaw::core::GatewayRuntimeBootstrapCoordinator coordinator;

	std::vector<std::string> traces;
	const auto warnings = coordinator.RunClosePrelude(
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator::CloseContext{
			.gatewayHost = host,
			.appendTrace = [&traces](const char* stage)
			{
				traces.emplace_back(stage == nullptr ? "" : stage);
			},
		});

	REQUIRE(traces.size() == 2);
	REQUIRE(traces[0] == "GatewayRuntimeBootstrap.ClosePrelude.begin");
	REQUIRE(traces[1] == "GatewayRuntimeBootstrap.ClosePrelude.done");
	REQUIRE(warnings.empty());
}

TEST_CASE(
	"GatewayRuntimeBootstrapCoordinator startup failure cleanup stops running host",
	"[gateway][lifecycle][p2]")
{
	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());
	REQUIRE(host.IsRunning());

	blazeclaw::config::AppConfig config;
	blazeclaw::core::GatewayRuntimeBootstrapCoordinator coordinator;
	std::vector<std::string> traces;
	coordinator.HandleStartupFailure(
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator::StartupContext{
			.config = config,
			.gatewayHost = host,
			.appendTrace = [&traces](const char* stage)
			{
				traces.emplace_back(stage == nullptr ? "" : stage);
			},
		},
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator::StartupResult{
			.success = false,
			.failedStage = "start_post_attach_runtime",
		});

	REQUIRE_FALSE(host.IsRunning());
	REQUIRE(traces.size() == 2);
	REQUIRE(traces.front() ==
		"GatewayRuntimeBootstrap.StartupFailureCleanup.begin");
	REQUIRE(traces.back() ==
		"GatewayRuntimeBootstrap.StartupFailureCleanup.done");
}

TEST_CASE(
	"GatewayRuntimeBootstrapCoordinator startup mode matrix yields expected lifecycle state",
	"[gateway][lifecycle][p2][startup-mode]")
{
	struct StartupModeExpectation
	{
		std::wstring configuredMode;
		std::string selectedMode;
		bool expectDegraded;
	};

	const std::vector<StartupModeExpectation> expectations = {
		StartupModeExpectation{ L"disabled", "disabled", true },
		StartupModeExpectation{ L"local_runtime_dispatch", "local_runtime_dispatch", true },
		StartupModeExpectation{ L"local_only", "local_only", false },
		StartupModeExpectation{ L"full", "full_gateway", false },
	};

	for (const auto& expectation : expectations) {
		blazeclaw::config::AppConfig config;
		config.gateway.startupMode = expectation.configuredMode;
		config.gateway.bindAddress = L"127.0.0.1";
		config.gateway.port = static_cast<std::uint16_t>(57200);

		blazeclaw::gateway::GatewayHost host;
		blazeclaw::core::GatewayRuntimeBootstrapCoordinator coordinator;
		const auto startupResult = coordinator.ExecuteStartup(
			blazeclaw::core::GatewayRuntimeBootstrapCoordinator::StartupContext{
				.config = config,
				.gatewayHost = host,
				.appendTrace = nullptr,
			});

		REQUIRE(startupResult.selectedMode == expectation.selectedMode);
		if (expectation.configuredMode == L"full") {
			if (startupResult.success) {
				REQUIRE(startupResult.gatewayStarted);
			}
		}
		else {
			REQUIRE(startupResult.success);
		}

		REQUIRE(startupResult.degraded == expectation.expectDegraded);

		coordinator.HandleStartupFailure(
			blazeclaw::core::GatewayRuntimeBootstrapCoordinator::StartupContext{
				.config = config,
				.gatewayHost = host,
				.appendTrace = nullptr,
			},
			startupResult);
	}
}

TEST_CASE(
	"GatewayManagedConfigReloader applies and rejects config transitions",
	"[gateway][lifecycle][p2]")
{
	const auto path = CreateTempConfigPath(L"apply_reject");
	WriteConfigFile(
		path,
		"gateway.port=56789\n"
		"chat.activeProvider=local\n"
		"chat.activeModel=default\n");

	blazeclaw::core::GatewayManagedConfigReloader reloader;
	blazeclaw::config::AppConfig initialConfig;
	std::size_t callbackApplyCount = 0;
	std::size_t callbackRejectCount = 0;
	std::vector<std::wstring> warnings;

	reloader.Start(
		initialConfig,
		blazeclaw::core::GatewayManagedConfigReloader::Options{
			.configPath = path,
			.pollIntervalMs = 0,
		},
		blazeclaw::core::GatewayManagedConfigReloader::Callbacks{
			.appendTrace = nullptr,
			.onWarning = [&warnings](const std::wstring& warning)
			{
				warnings.push_back(warning);
			},
			.applyConfigDiff = [
				&callbackApplyCount,
				&callbackRejectCount](
					const blazeclaw::config::AppConfig& next,
					std::wstring& warning)
			{
				if (next.chat.activeProvider == L"reject") {
					++callbackRejectCount;
					warning = L"rejected provider transition";
					return false;
				}

				++callbackApplyCount;
				return true;
			},
		});

	reloader.Pump();
	REQUIRE(reloader.ApplyCount() == 0);
	REQUIRE(reloader.RejectCount() == 0);

	WriteConfigFile(
		path,
		"gateway.port=56789\n"
		"chat.activeProvider=deepseek\n"
		"chat.activeModel=deepseek-chat\n");
	reloader.Pump();
	REQUIRE(reloader.ApplyCount() == 1);
	REQUIRE(callbackApplyCount == 1);

	WriteConfigFile(
		path,
		"gateway.port=56789\n"
		"chat.activeProvider=reject\n"
		"chat.activeModel=blocked\n");
	reloader.Pump();
	REQUIRE(reloader.RejectCount() == 1);
	REQUIRE(callbackRejectCount == 1);
	REQUIRE_FALSE(warnings.empty());

	reloader.Stop();
	REQUIRE_FALSE(reloader.IsRunning());

	std::error_code removeError;
	std::filesystem::remove(path, removeError);
}

TEST_CASE(
	"GatewayManagedConfigReloader skips internally tagged writes",
	"[gateway][lifecycle][p2][reloader]")
{
	const auto path = CreateTempConfigPath(L"internal_write_skip");
	const std::string initialContent =
		"gateway.port=56789\n"
		"chat.activeProvider=local\n"
		"chat.activeModel=default\n";
	WriteConfigFile(path, initialContent);

	blazeclaw::core::GatewayManagedConfigReloader reloader;
	blazeclaw::config::AppConfig initialConfig;
	std::size_t callbackApplyCount = 0;

	reloader.Start(
		initialConfig,
		blazeclaw::core::GatewayManagedConfigReloader::Options{
			.configPath = path,
			.pollIntervalMs = 0,
		},
		blazeclaw::core::GatewayManagedConfigReloader::Callbacks{
			.appendTrace = nullptr,
			.onWarning = nullptr,
			.applyConfigDiff = [&callbackApplyCount](
				const blazeclaw::config::AppConfig&,
				std::wstring&)
			{
				++callbackApplyCount;
				return true;
			},
		});

	const std::string internalWriteContent =
		"gateway.port=56789\n"
		"chat.activeProvider=deepseek\n"
		"chat.activeModel=deepseek-chat\n";
	WriteConfigFile(path, internalWriteContent);
	reloader.RegisterInternalWriteHash(
		ComputeFileContentHash(path));
	reloader.Pump();

	REQUIRE(reloader.ApplyCount() == 0);
	REQUIRE(reloader.RejectCount() == 0);
	REQUIRE(callbackApplyCount == 0);

	reloader.Stop();

	std::error_code removeError;
	std::filesystem::remove(path, removeError);
}

TEST_CASE(
	"TransportRecipientRegistry handles reconnect and multi-client recipient lifecycle",
	"[gateway][lifecycle][p2][recipient]")
{
	blazeclaw::gateway::TransportRecipientRegistry registry;
	const std::uint64_t nowMs = 1000;

	registry.RegisterRecipient("run-1", "session-a", "conn-1", nowMs);
	registry.RegisterLateJoin("session-a", "conn-2", nowMs + 1);
	registry.RegisterLateJoin("session-a", "conn-3", nowMs + 2);
	registry.MarkRunFinalized("run-1", nowMs + 3);

	auto recipients = registry.RecipientsForRun("run-1");
	REQUIRE(recipients.size() == 3);
	REQUIRE(recipients.contains("conn-1"));
	REQUIRE(recipients.contains("conn-2"));
	REQUIRE(recipients.contains("conn-3"));

	registry.PruneDisconnected("conn-1");
	recipients = registry.RecipientsForRun("run-1");
	REQUIRE(recipients.size() == 2);

	registry.PruneDisconnected("conn-2");
	registry.PruneDisconnected("conn-3");
	REQUIRE_FALSE(registry.HasRecipients("run-1"));

	registry.RegisterRecipient("run-2", "session-a", "conn-4", nowMs + 10);
	registry.PruneDisconnected("conn-4");
	REQUIRE_FALSE(registry.HasRecipients("run-2"));
	REQUIRE(registry.GetSnapshot().runCount == 1);

	registry.PruneExpired(nowMs + (16 * 60 * 1000));
	REQUIRE(registry.GetSnapshot().runCount == 0);
}

TEST_CASE(
	"Diagnostics report includes gateway lifecycle startup visibility fields",
	"[diagnostics][gateway][p2]")
{
	blazeclaw::core::DiagnosticsSnapshot snapshot;
	snapshot.runtimeRunning = true;
	snapshot.gatewayWarning = "";
	snapshot.gatewayStartupMode = "local_runtime_dispatch";
	snapshot.gatewayStartupModeSource = "config";
	snapshot.gatewayStartupFailedStage = "";
	snapshot.gatewayStartupDegraded = true;
	snapshot.gatewayManagedConfigReloaderStarted = true;
	snapshot.gatewayManagedConfigReloaderRunning = true;
	snapshot.gatewayClosePreludeExecuted = false;
	snapshot.gatewayRuntimeStateCreated = true;
	snapshot.gatewayRuntimeServicesStarted = true;
	snapshot.gatewayTransportHandlersAttached = false;
	snapshot.gatewayRuntimeSubscriptionsStarted = true;
	snapshot.gatewayManagedConfigPath = "blazeclaw.conf";
	snapshot.gatewayManagedConfigApplyCount = 3;
	snapshot.gatewayManagedConfigRejectCount = 1;
	snapshot.gatewayAuthSessionGenerationCurrent = 4;
	snapshot.gatewayAuthSessionGenerationRequired = 5;
	snapshot.gatewayAuthSessionGenerationRejectCount = 2;
	snapshot.gatewayStartupFailureCleanupExecuted = true;
	snapshot.gatewayCleanupPath = "startup_failure";

	blazeclaw::core::CDiagnosticsReportBuilder builder;
	const std::string report = builder.BuildOperatorDiagnosticsReport(snapshot);

	REQUIRE(report.find("\"gatewayLifecycle\"") != std::string::npos);
	REQUIRE(
		report.find("\"startupMode\":\"local_runtime_dispatch\"") !=
		std::string::npos);
	REQUIRE(
		report.find("\"managedConfigReloaderRunning\":true") !=
		std::string::npos);
	REQUIRE(
		report.find("\"managedConfigApplyCount\":3") !=
		std::string::npos);
	REQUIRE(
		report.find("\"managedConfigRejectCount\":1") !=
		std::string::npos);
	REQUIRE(
		report.find("\"authSessionGenerationCurrent\":4") !=
		std::string::npos);
	REQUIRE(
		report.find("\"authSessionGenerationRequired\":5") !=
		std::string::npos);
	REQUIRE(
		report.find("\"authSessionGenerationRejectCount\":2") !=
		std::string::npos);
	REQUIRE(
		report.find("\"startupFailureCleanupExecuted\":true") !=
		std::string::npos);
	REQUIRE(
		report.find("\"cleanupPath\":\"startup_failure\"") !=
		std::string::npos);
}
