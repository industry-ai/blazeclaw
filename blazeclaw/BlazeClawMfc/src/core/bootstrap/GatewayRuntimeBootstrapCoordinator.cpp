#include "pch.h"
#include "GatewayRuntimeBootstrapCoordinator.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>

namespace blazeclaw::core {

	namespace {

		std::wstring TrimWide(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ReadWideEnvironment(const wchar_t* key) {
			if (key == nullptr || *key == L'\0') {
				return {};
			}

			wchar_t* raw = nullptr;
			size_t rawSize = 0;
			if (_wdupenv_s(&raw, &rawSize, key) != 0 ||
				raw == nullptr ||
				rawSize == 0) {
				return {};
			}

			const std::wstring value = TrimWide(raw);
			free(raw);
			return value;
		}

		std::wstring ToLowerWide(std::wstring value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return value;
		}

	} // namespace

	GatewayRuntimeBootstrapCoordinator::StartupDecision
		GatewayRuntimeBootstrapCoordinator::PrepareRuntimeConfig(
			const blazeclaw::config::GatewayConfig& gatewayConfig) const {
		StartupDecision decision;

		const std::wstring configuredMode = ToLowerWide(
			TrimWide(gatewayConfig.startupMode));
		decision.mode = ParseStartupModeLabel(configuredMode);
		decision.modeLabel = StartupModeLabel(decision.mode);
		decision.modeSource = "config";

		const std::wstring envMode = ToLowerWide(
			ReadWideEnvironment(L"BLAZECLAW_GATEWAY_STARTUP_MODE"));
		if (!envMode.empty()) {
			decision.mode = ParseStartupModeLabel(envMode);
			decision.modeLabel = StartupModeLabel(decision.mode);
			decision.modeSource = "env";
		}

		const std::wstring managedReloaderMode = ToLowerWide(
			ReadWideEnvironment(L"BLAZECLAW_GATEWAY_MANAGED_RELOADER_ENABLED"));
		if (managedReloaderMode == L"0" ||
			managedReloaderMode == L"false" ||
			managedReloaderMode == L"off") {
			decision.startManagedConfigReloader = false;
		}

		return decision;
	}

	GatewayRuntimeBootstrapCoordinator::StartupMode
		GatewayRuntimeBootstrapCoordinator::ParseStartupModeLabel(
			const std::wstring& raw) {
		if (raw == L"disabled") {
			return StartupMode::Disabled;
		}

		if (raw == L"local_only") {
			return StartupMode::LocalOnly;
		}

		if (raw == L"full" || raw == L"transport") {
			return StartupMode::FullGateway;
		}

		return StartupMode::LocalRuntimeDispatch;
	}

	std::string GatewayRuntimeBootstrapCoordinator::StartupModeLabel(
		StartupMode mode) {
		switch (mode) {
		case StartupMode::Disabled:
			return "disabled";
		case StartupMode::LocalOnly:
			return "local_only";
		case StartupMode::FullGateway:
			return "full_gateway";
		case StartupMode::LocalRuntimeDispatch:
		default:
			return "local_runtime_dispatch";
		}
	}

	bool GatewayRuntimeBootstrapCoordinator::CreateRuntimeState(
		const StartupContext& context,
		StartupResult& result) const {
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.CreateRuntimeState.begin");
		}

		result.failedStage = "create_runtime_state";

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.CreateRuntimeState.ready");
		}

		return true;
	}

	bool GatewayRuntimeBootstrapCoordinator::StartEarlyRuntime(
		const StartupContext& context,
		const StartupDecision& decision,
		StartupResult& result) const {
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartEarlyRuntime.begin");
		}

		result.failedStage = "start_early_runtime";
		if (decision.mode == StartupMode::Disabled ||
			decision.mode == StartupMode::LocalOnly ||
			decision.mode == StartupMode::LocalRuntimeDispatch) {
			result.localDispatchReady = context.gatewayHost.StartLocalRuntimeDispatchOnly();
			if (!result.localDispatchReady) {
				result.warnings.push_back(
					L"gateway local runtime dispatch initialization failed.");
				return false;
			}
		}

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartEarlyRuntime.ready");
		}

		return true;
	}

	bool GatewayRuntimeBootstrapCoordinator::AttachTransportHandlers(
		const StartupContext& context,
		const StartupDecision& decision,
		StartupResult& result) const {
		UNREFERENCED_PARAMETER(decision);

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.AttachTransportHandlers.begin");
		}

		result.failedStage = "attach_transport_handlers";

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.AttachTransportHandlers.ready");
		}

		return true;
	}

	bool GatewayRuntimeBootstrapCoordinator::StartPostAttachRuntime(
		const StartupContext& context,
		const StartupDecision& decision,
		StartupResult& result) const {
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartPostAttachRuntime.begin");
		}

		result.failedStage = "start_post_attach_runtime";
		switch (decision.mode) {
		case StartupMode::Disabled:
			result.degraded = true;
			result.warnings.push_back(
				L"gateway startup mode is disabled; running without gateway transport.");
			break;
		case StartupMode::LocalRuntimeDispatch:
			result.degraded = true;
			result.localRuntimeReady = true;
			break;
		case StartupMode::LocalOnly:
			result.localRuntimeReady = context.gatewayHost.StartLocalOnly(context.config.gateway);
			if (!result.localRuntimeReady) {
				result.warnings.push_back(
					L"gateway local runtime initialization failed.");
				return false;
			}
			break;
		case StartupMode::FullGateway:
			result.gatewayStarted = context.gatewayHost.Start(context.config.gateway);
			if (!result.gatewayStarted) {
				result.warnings.push_back(
					L"gateway transport startup failed.");
				return false;
			}
			break;
		}

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartPostAttachRuntime.ready");
		}

		return true;
	}

	bool GatewayRuntimeBootstrapCoordinator::StartManagedConfigReloader(
		const StartupContext& context,
		const StartupDecision& decision,
		StartupResult& result) const {
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartManagedConfigReloader.begin");
		}

		result.failedStage = "start_managed_config_reloader";
		result.managedConfigReloaderStarted = decision.startManagedConfigReloader;

		if (context.appendTrace) {
			context.appendTrace(
				result.managedConfigReloaderStarted
				? "GatewayRuntimeBootstrap.StartManagedConfigReloader.ready"
				: "GatewayRuntimeBootstrap.StartManagedConfigReloader.skipped");
		}

		return true;
	}

	GatewayRuntimeBootstrapCoordinator::StartupResult
		GatewayRuntimeBootstrapCoordinator::ExecuteStartup(
			const StartupContext& context) const {
		StartupResult result;
		const StartupDecision decision = PrepareRuntimeConfig(context.config.gateway);
		result.selectedMode = decision.modeLabel;
		result.selectedModeSource = decision.modeSource;
		result.failedStage = "prepare_runtime_config";

		if (!CreateRuntimeState(context, result)) {
			return result;
		}

		if (!StartEarlyRuntime(context, decision, result)) {
			return result;
		}

		if (!AttachTransportHandlers(context, decision, result)) {
			return result;
		}

		if (!StartPostAttachRuntime(context, decision, result)) {
			return result;
		}

		if (!StartManagedConfigReloader(context, decision, result)) {
			return result;
		}

		result.success = true;
		result.failedStage.clear();
		return result;
	}

	void GatewayRuntimeBootstrapCoordinator::HandleStartupFailure(
		const StartupContext& context,
		const StartupResult& result) const {
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartupFailureCleanup.begin");
		}

		context.gatewayHost.Stop();
		UNREFERENCED_PARAMETER(result);

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.StartupFailureCleanup.done");
		}
	}

	std::vector<std::wstring> GatewayRuntimeBootstrapCoordinator::RunClosePrelude(
		const CloseContext& context) const {
		std::vector<std::wstring> warnings;
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.ClosePrelude.begin");
		}

		std::string pumpError;
		if (context.gatewayHost.IsRunning() &&
			!context.gatewayHost.PumpNetworkOnce(pumpError) &&
			!pumpError.empty()) {
			warnings.push_back(
				L"gateway close prelude network pump failed; continuing shutdown.");
		}

		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.ClosePrelude.done");
		}

		return warnings;
	}

} // namespace blazeclaw::core
