#pragma once

#include "../../config/ConfigModels.h"
#include "../../gateway/GatewayHost.h"

#include <functional>
#include <string>
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
		};

		struct StartupContext {
			const blazeclaw::config::AppConfig& config;
			blazeclaw::gateway::GatewayHost& gatewayHost;
			std::function<void(const char*)> appendTrace;
		};

		struct CloseContext {
			blazeclaw::gateway::GatewayHost& gatewayHost;
			std::function<void(const char*)> appendTrace;
		};

		[[nodiscard]] StartupResult ExecuteStartup(const StartupContext& context) const;
		void HandleStartupFailure(const StartupContext& context, const StartupResult& result) const;
		[[nodiscard]] std::vector<std::wstring> RunClosePrelude(const CloseContext& context) const;

	private:
		[[nodiscard]] StartupDecision PrepareRuntimeConfig(
			const blazeclaw::config::GatewayConfig& gatewayConfig) const;
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
