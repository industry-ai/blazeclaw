#include "pch.h"
#include "GatewayRuntimeBootstrapCoordinator.h"

#include "../../config/ConfigModels.h"
#include "../../gateway/GatewayNetPolicy.h"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core {

	namespace {

		const std::vector<std::string_view> kNormalStopOwnedCleanupOrder = {
			"plugin_global_stop",
			"gateway_host_stop",
			"gateway_close_prelude",
			"non_gateway_runtime_cleanup",
			"managed_config_reloader_stop",
		};

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

		bool IsLoopbackBind(const std::wstring& bind) {
			const std::wstring trimmed = TrimWide(bind);
			if (trimmed.empty()) {
				return true;
			}
			const std::wstring lower = ToLowerWide(trimmed);
			return lower == L"127.0.0.1" || lower == L"localhost" || lower == L"::1" ||
				lower == L"loopback";
		}

		bool IsKnownStartupModeToken(const std::wstring& w) {
			return w == L"disabled" || w == L"local_only" || w == L"local_runtime_dispatch" ||
				w == L"full" || w == L"transport";
		}

		std::string Utf8Narrow(const std::wstring& w) {
			if (w.empty()) {
				return {};
			}

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				w.c_str(),
				-1,
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 1) {
				return {};
			}

			std::string out(static_cast<std::size_t>(required - 1), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				w.c_str(),
				-1,
				out.data(),
				required,
				nullptr,
				nullptr);
			return out;
		}

		std::optional<std::uint16_t> TryParseUInt16(const std::wstring& raw) {
			if (raw.empty()) {
				return std::nullopt;
			}
			wchar_t* end = nullptr;
			const unsigned long v = std::wcstoul(raw.c_str(), &end, 10);
			if (end == raw.c_str() || v < 1ul || v > 65535ul) {
				return std::nullopt;
			}
			return static_cast<std::uint16_t>(v);
		}

		std::optional<std::uint64_t> TryParseUInt64(const std::wstring& raw) {
			if (raw.empty()) {
				return std::nullopt;
			}
			wchar_t* end = nullptr;
			const unsigned long long v = std::wcstoull(raw.c_str(), &end, 10);
			if (end == raw.c_str()) {
				return std::nullopt;
			}
			return static_cast<std::uint64_t>(v);
		}

		std::optional<blazeclaw::gateway::GatewayNetPolicy::BindMode> TryParseBindModeWide(
			const std::wstring& raw) {
			const std::wstring lower = ToLowerWide(TrimWide(raw));
			if (lower.empty()) {
				return std::nullopt;
			}
			if (lower == L"loopback") {
				return blazeclaw::gateway::GatewayNetPolicy::BindMode::Loopback;
			}
			if (lower == L"lan") {
				return blazeclaw::gateway::GatewayNetPolicy::BindMode::Lan;
			}
			if (lower == L"tailnet") {
				return blazeclaw::gateway::GatewayNetPolicy::BindMode::Tailnet;
			}
			if (lower == L"auto") {
				return blazeclaw::gateway::GatewayNetPolicy::BindMode::Auto;
			}
			if (lower == L"custom") {
				return blazeclaw::gateway::GatewayNetPolicy::BindMode::Custom;
			}
			return std::nullopt;
		}

		std::wstring Utf8ToWideConfig(const std::string& u8) {
			if (u8.empty()) {
				return L"127.0.0.1";
			}
			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				u8.c_str(),
				static_cast<int>(u8.size()),
				nullptr,
				0);
			if (required <= 0) {
				return L"127.0.0.1";
			}
			std::wstring out(static_cast<std::size_t>(required), L'\0');
			MultiByteToWideChar(
				CP_UTF8,
				0,
				u8.c_str(),
				static_cast<int>(u8.size()),
				out.data(),
				required);
			return out;
		}

	} // namespace

	const std::vector<std::string_view>&
		GatewayRuntimeBootstrapCoordinator::NormalStopGatewayOwnedCleanupExecutionOrder() noexcept {
		return kNormalStopOwnedCleanupOrder;
	}

	blazeclaw::config::GatewayResolvedRuntimeConfig
		GatewayRuntimeBootstrapCoordinator::ResolveGatewayRuntimeConfig(
			const blazeclaw::config::AppConfig& appConfig) const {
		blazeclaw::config::GatewayResolvedRuntimeConfig r{};

		const blazeclaw::config::GatewayConfig& gw = appConfig.gateway;

		// bind: legacy string path + OpenClaw `net.ts` bind mode policy (N3) when
		// `gateway.bind.mode` or `BLAZECLAW_GATEWAY_BIND_MODE` is set.
		r.effectiveBindMode = "unspecified";
		r.bindModeSource = "legacy";
		std::string addressFromConfig = Utf8Narrow(TrimWide(gw.bindAddress));
		if (addressFromConfig.empty()) {
			addressFromConfig = "127.0.0.1";
		}
		r.bindAddressUtf8 = addressFromConfig;
		r.bindSource = "config";

		{
			std::wstring modeWide = TrimWide(gw.bindMode);
			std::string modeSource = "config";
			const std::wstring modeEnv = TrimWide(ReadWideEnvironment(L"BLAZECLAW_GATEWAY_BIND_MODE"));
			if (!modeEnv.empty()) {
				modeWide = modeEnv;
				modeSource = "env";
			}
			if (const auto mode = TryParseBindModeWide(modeWide)) {
				r.bindModeSource = modeSource;
				const std::string label = Utf8Narrow(ToLowerWide(TrimWide(modeWide)));
				r.effectiveBindMode = label.empty() ? "unspecified" : label;
				const std::function<bool(const std::string& host)> canBind =
					[](const std::string& h) {
						return blazeclaw::gateway::GatewayNetPolicy::CanBindToHost(h);
					};
				r.bindAddressUtf8 = blazeclaw::gateway::GatewayNetPolicy::ResolveGatewayBindHost(
					*mode,
					std::make_optional(addressFromConfig),
					[]() { return std::string(); },
					canBind,
					blazeclaw::gateway::GatewayNetPolicy::IsContainerEnvironment());
			}
		}

		{
			const std::wstring bindEnv = TrimWide(ReadWideEnvironment(L"BLAZECLAW_GATEWAY_BIND"));
			if (!bindEnv.empty()) {
				const std::string n = Utf8Narrow(bindEnv);
				if (!n.empty()) {
					r.bindAddressUtf8 = n;
					r.bindSource = "env";
				}
			}
		}

		// port
		r.port = gw.port;
		r.portSource = "config";
		{
			const std::wstring penv = TrimWide(ReadWideEnvironment(L"BLAZECLAW_GATEWAY_PORT"));
			if (const auto parsed = TryParseUInt16(penv)) {
				r.port = *parsed;
				r.portSource = "env";
			}
		}

		// auth session generation
		r.authSessionGeneration = gw.authSessionGeneration;
		r.authSessionGenerationSource = "config";
		{
			const std::wstring aenv = TrimWide(
				ReadWideEnvironment(L"BLAZECLAW_GATEWAY_AUTH_SESSION_GENERATION"));
			if (const auto parsed = TryParseUInt64(aenv)) {
				r.authSessionGeneration = *parsed;
				r.authSessionGenerationSource = "env";
			}
		}

		// managed config reloader
		r.managedConfigReloader = true;
		r.managedConfigReloaderSource = "default";
		{
			const std::wstring rel = ToLowerWide(
				TrimWide(ReadWideEnvironment(L"BLAZECLAW_GATEWAY_MANAGED_RELOADER_ENABLED")));
			if (rel == L"0" || rel == L"false" || rel == L"off") {
				r.managedConfigReloader = false;
				r.managedConfigReloaderSource = "env";
			}
		}

		// startup mode: config, optional env override, invalid-token fallback
		std::wstring modeWide = ToLowerWide(TrimWide(gw.startupMode));
		r.startupModeSource = "config";
		{
			const std::wstring envMode = ToLowerWide(
				TrimWide(ReadWideEnvironment(L"BLAZECLAW_GATEWAY_STARTUP_MODE")));
			if (!envMode.empty()) {
				modeWide = envMode;
				r.startupModeSource = "env";
			}
		}
		if (modeWide.empty()) {
			modeWide = L"local_runtime_dispatch";
		}
		if (!IsKnownStartupModeToken(modeWide)) {
			r.startupModeUnrecognizedInputUtf8 = Utf8Narrow(modeWide);
			r.startupModeInvalidFallback = true;
			modeWide = L"local_runtime_dispatch";
		}

		const StartupMode sm = ParseStartupModeLabel(modeWide);
		r.startupModeClass = static_cast<blazeclaw::config::GatewayResolvedStartupModeClass>(
			static_cast<std::uint8_t>(static_cast<int>(sm)));
		r.effectiveStartupModeLabel = StartupModeLabel(sm);
		return r;
	}

	GatewayRuntimeBootstrapCoordinator::StartupDecision
		GatewayRuntimeBootstrapCoordinator::DecisionFromResolved(
			const blazeclaw::config::GatewayResolvedRuntimeConfig& resolved) const {
		StartupDecision d;
		d.mode = static_cast<StartupMode>(static_cast<int>(resolved.startupModeClass));
		d.modeLabel = resolved.effectiveStartupModeLabel;
		d.modeSource = resolved.startupModeSource;
		d.startManagedConfigReloader = resolved.managedConfigReloader;
		return d;
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
		if (result.selectedMode == "full_gateway" ||
			result.selectedMode == "local_only") {
			if (!context.gatewayHost.BootstrapCreateRuntimeState(
				context.config.gateway)) {
				result.warnings.push_back(
					L"gateway runtime state creation failed.");
				return false;
			}
		}

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
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.AttachTransportHandlers.begin");
		}

		result.failedStage = "attach_transport_handlers";
		if (decision.mode == StartupMode::FullGateway ||
			decision.mode == StartupMode::LocalOnly) {
			if (!context.gatewayHost.BootstrapStartRuntimeServices()) {
				result.warnings.push_back(
					L"gateway runtime services startup failed.");
				return false;
			}

			if (!context.gatewayHost.BootstrapAttachTransportHandlers()) {
				result.warnings.push_back(
					L"gateway transport handler attach failed.");
				return false;
			}

			if (!context.gatewayHost.BootstrapStartRuntimeSubscriptions()) {
				result.warnings.push_back(
					L"gateway runtime subscription startup failed.");
				return false;
			}

			if (!context.gatewayHost.BootstrapFinalizeRuntimeInitialization()) {
				result.warnings.push_back(
					L"gateway runtime initialization finalization failed.");
				return false;
			}
		}

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

	void GatewayRuntimeBootstrapCoordinator::RunStartupMigrations(
		const StartupContext& context) const {
		if (!context.appendTrace) {
			return;
		}
		if (context.suppressStartupMigrations) {
			context.appendTrace("GatewayRuntimeBootstrap.migration.suppressed");
			return;
		}
		if (context.appliedStartupMigrationsOut == nullptr) {
			return;
		}
		const char* const kId = blazeclaw::config::GatewayMigrationIds::kControlUiNonLoopbackBindParityV1;
		const auto& applied = *context.appliedStartupMigrationsOut;
		if (std::find(applied.begin(), applied.end(), kId) != applied.end()) {
			context.appendTrace("GatewayRuntimeBootstrap.migration.already_applied");
			return;
		}
		if (IsLoopbackBind(context.config.gateway.bindAddress)) {
			context.appendTrace(
				"GatewayRuntimeBootstrap.migration.control_ui_nonloopback_bind_parity_v1.skipped");
			return;
		}
		context.appliedStartupMigrationsOut->push_back(kId);
		context.appendTrace(
			"GatewayRuntimeBootstrap.migration.control_ui_nonloopback_bind_parity_v1.applied");
		if (context.queueManagedConfigInternalWriteHash) {
			const std::uint64_t h =
				std::hash<std::string>{}(std::string(kId));
			context.queueManagedConfigInternalWriteHash(h);
		}
	}

	GatewayRuntimeBootstrapCoordinator::StartupResult
		GatewayRuntimeBootstrapCoordinator::ExecuteStartup(
			const StartupContext& context) const {
		StartupResult result;
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.ExecuteStartup.begin");
		}
		RunStartupMigrations(context);
		result.resolvedRuntime = ResolveGatewayRuntimeConfig(context.config);
		const StartupDecision decision = DecisionFromResolved(result.resolvedRuntime);
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.ResolveRuntimeConfig.ready");
		}
		result.selectedMode = decision.modeLabel;
		result.selectedModeSource = decision.modeSource;
		result.failedStage = "prepare_runtime_config";

		blazeclaw::config::AppConfig effectiveConfig = context.config;
		effectiveConfig.gateway.bindAddress = Utf8ToWideConfig(result.resolvedRuntime.bindAddressUtf8);
		const StartupContext effectiveContext{
			.config = effectiveConfig,
			.gatewayHost = context.gatewayHost,
			.appendTrace = context.appendTrace,
			.queueManagedConfigInternalWriteHash = context.queueManagedConfigInternalWriteHash,
			.suppressStartupMigrations = context.suppressStartupMigrations,
			.appliedStartupMigrationsOut = context.appliedStartupMigrationsOut,
		};

		if (!CreateRuntimeState(effectiveContext, result)) {
			return result;
		}

		if (!StartEarlyRuntime(context, decision, result)) {
			return result;
		}

		if (!AttachTransportHandlers(context, decision, result)) {
			return result;
		}

		if (!StartPostAttachRuntime(effectiveContext, decision, result)) {
			return result;
		}

		if (!StartManagedConfigReloader(context, decision, result)) {
			return result;
		}

		result.success = true;
		result.failedStage.clear();
		if (context.appendTrace) {
			context.appendTrace("GatewayRuntimeBootstrap.ExecuteStartup.success");
		}
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
