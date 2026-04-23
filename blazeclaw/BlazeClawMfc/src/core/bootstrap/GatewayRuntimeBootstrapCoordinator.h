#pragma once

#include "../../config/ConfigModels.h"
#include "../../gateway/GatewayHost.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace blazeclaw::core {

	class GatewayRuntimeBootstrapCoordinator {
	public:
		enum class StartupMode {
			Disabled,
			LocalRuntimeDispatch,
			LocalOnly,
			FullGateway,
		};

		struct StartupDecision {
			StartupMode mode = StartupMode::LocalRuntimeDispatch;
			bool startManagedConfigReloader = true;
			std::string modeLabel = "local_runtime_dispatch";
			std::string modeSource = "config";
		};

		struct StartupResult {
			bool success = false;
			bool degraded = false;
			bool gatewayStarted = false;
			bool localDispatchReady = false;
			bool localRuntimeReady = false;
			bool managedConfigReloaderStarted = false;
			std::string selectedMode = "local_runtime_dispatch";
			std::string selectedModeSource = "config";
			std::string failedStage;
			std::vector<std::wstring> warnings;
			blazeclaw::config::GatewayResolvedRuntimeConfig resolvedRuntime;
		};

		struct StartupContext {
			const blazeclaw::config::AppConfig& config;
			blazeclaw::gateway::GatewayHost& gatewayHost;
			std::function<void(const char*)> appendTrace;
			/// S1: optional; used when a migration would queue a managed-config internal write hash.
			std::function<void(std::uint64_t)> queueManagedConfigInternalWriteHash;
			/// S1: `BLAZECLAW_GATEWAY_SUPPRESS_STARTUP_MIGRATIONS=1` suppresses writes/traces.
			bool suppressStartupMigrations = false;
			/// S1: receives stable migration ids when applied (e.g. control-ui bind parity).
			std::vector<std::string>* appliedStartupMigrationsOut = nullptr;
		};

		struct CloseContext {
			blazeclaw::gateway::GatewayHost& gatewayHost;
			std::function<void(const char*)> appendTrace;
		};

		[[nodiscard]] StartupResult ExecuteStartup(const StartupContext& context) const;
		/// OpenClaw `resolveGatewayRuntimeConfig` equivalent: config + env effective policy.
		[[nodiscard]] blazeclaw::config::GatewayResolvedRuntimeConfig ResolveGatewayRuntimeConfig(
			const blazeclaw::config::AppConfig& appConfig) const;
		void HandleStartupFailure(const StartupContext& context, const StartupResult& result) const;
		[[nodiscard]] std::vector<std::wstring> RunClosePrelude(const CloseContext& context) const;

		/// S5.1: Intended `RegisterGatewayOwnedRuntimeCleanup` LIFO execution order (last registered runs first).
		[[nodiscard]] static const std::vector<std::string_view>&
			NormalStopGatewayOwnedCleanupExecutionOrder() noexcept;

	private:
		void RunStartupMigrations(
			const StartupContext& context) const;
		[[nodiscard]] StartupDecision DecisionFromResolved(
			const blazeclaw::config::GatewayResolvedRuntimeConfig& resolved) const;
		[[nodiscard]] static StartupMode ParseStartupModeLabel(const std::wstring& raw);
		[[nodiscard]] static std::string StartupModeLabel(StartupMode mode);
		[[nodiscard]] bool CreateRuntimeState(
			const StartupContext& context,
			StartupResult& result) const;
		[[nodiscard]] bool StartEarlyRuntime(
			const StartupContext& context,
			const StartupDecision& decision,
			StartupResult& result) const;
		[[nodiscard]] bool AttachTransportHandlers(
			const StartupContext& context,
			const StartupDecision& decision,
			StartupResult& result) const;
		[[nodiscard]] bool StartPostAttachRuntime(
			const StartupContext& context,
			const StartupDecision& decision,
			StartupResult& result) const;
		[[nodiscard]] bool StartManagedConfigReloader(
			const StartupContext& context,
			const StartupDecision& decision,
			StartupResult& result) const;
	};

} // namespace blazeclaw::core
