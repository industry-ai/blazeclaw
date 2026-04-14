#include "pch.h"
#include "PluginRuntimeStateService.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace blazeclaw::gateway {

	namespace {

		std::string ToLowerCopy(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

	} // namespace

	PluginRuntimeStateService::PluginRuntimeStateService()
		: m_transitionPolicy(TransitionPolicySettings{
			.historyLimit = 128,
			.exportEnabled = false,
			}) {}

	std::uint64_t PluginRuntimeStateService::CurrentEpochMs() {
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
			.count());
	}

	void PluginRuntimeStateService::RecordTransition(
		const std::string& action) {
		PluginRuntimeTransitionEntry entry;
		entry.sequence = ++m_transitionSequence;
		entry.timestampMs = CurrentEpochMs();
		entry.action = action;
		entry.severity = ResolveTransitionSeverity(action);
		entry.lifecyclePhase = ResolveTransitionLifecyclePhase(action);
		entry.activeVersion = m_state.activeVersion;
		entry.httpRouteVersion = GetHttpRouteVersion();
		entry.channelVersion = GetChannelVersion();
		entry.activeRegistryCount =
			m_state.activeRegistry == nullptr
			? 0
			: m_state.activeRegistry->size();
		entry.httpRoutePinned = m_state.httpRoute.pinned;
		entry.channelPinned = m_state.channel.pinned;
		entry.cacheKey = m_state.cacheKey;
		entry.workspaceDir = m_state.workspaceDir;
		entry.runtimeSubagentMode = m_state.runtimeSubagentMode;

		m_transitionHistory.push_back(std::move(entry));
		if (m_transitionHistory.size() > m_transitionPolicy.historyLimit) {
			const std::size_t overflow =
				m_transitionHistory.size() - m_transitionPolicy.historyLimit;
			m_transitionHistory.erase(
				m_transitionHistory.begin(),
				m_transitionHistory.begin() +
				static_cast<std::ptrdiff_t>(overflow));
		}
	}

	PluginRuntimeTransitionSeverity
		PluginRuntimeStateService::ResolveTransitionSeverity(
			const std::string& action) {
		const auto key = ToLowerCopy(action);
		if (key.find("reset") != std::string::npos) {
			return PluginRuntimeTransitionSeverity::Warn;
		}

		if (key.find("deactivate") != std::string::npos) {
			return PluginRuntimeTransitionSeverity::Warn;
		}

		if (key.find("release") != std::string::npos) {
			return PluginRuntimeTransitionSeverity::Warn;
		}

		return PluginRuntimeTransitionSeverity::Info;
	}

	PluginRuntimeLifecyclePhase
		PluginRuntimeStateService::ResolveTransitionLifecyclePhase(
			const std::string& action) {
		const auto key = ToLowerCopy(action);
		if (key.find("activate") != std::string::npos ||
			key.find("set_active") != std::string::npos ||
			key.find("require") != std::string::npos) {
			return PluginRuntimeLifecyclePhase::Activation;
		}

		if (key.find("deactivate") != std::string::npos) {
			return PluginRuntimeLifecyclePhase::Deactivation;
		}

		if (key.find("pin") != std::string::npos ||
			key.find("release") != std::string::npos) {
			return PluginRuntimeLifecyclePhase::Mutation;
		}

		if (key.find("reset") != std::string::npos) {
			return PluginRuntimeLifecyclePhase::Maintenance;
		}

		return PluginRuntimeLifecyclePhase::SteadyState;
	}

	void PluginRuntimeStateService::InstallSurfaceRegistry(
		PluginRuntimeSurfaceState& surface,
		const std::vector<ExtensionManifest>* registry,
		const bool pinned) {
		if (surface.registry == registry && surface.pinned == pinned) {
			return;
		}

		surface.registry = registry;
		surface.pinned = pinned;
		surface.version += 1;
	}

	void PluginRuntimeStateService::SyncSurface(
		PluginRuntimeSurfaceState& surface,
		const std::vector<ExtensionManifest>* registry,
		const bool refreshVersion) {
		if (surface.pinned) {
			return;
		}

		if (surface.registry == registry && !surface.pinned) {
			if (refreshVersion) {
				surface.version += 1;
			}
			return;
		}

		InstallSurfaceRegistry(surface, registry, false);
	}

	void PluginRuntimeStateService::SetActiveRegistry(
		const std::vector<ExtensionManifest>* registry,
		const std::string& cacheKey,
		const std::string& workspaceDir,
		const PluginRuntimeSubagentMode runtimeSubagentMode) {
		m_state.activeRegistry = registry;
		m_state.activeVersion += 1;
		SyncSurface(m_state.httpRoute, registry, true);
		SyncSurface(m_state.channel, registry, true);
		m_state.cacheKey = cacheKey;
		m_state.workspaceDir = workspaceDir;
		m_state.runtimeSubagentMode = runtimeSubagentMode;
		RecordTransition("set_active_registry");
	}

	void PluginRuntimeStateService::ActivateRuntimeRegistry(
		const std::vector<ExtensionManifest>* registry,
		const std::string& cacheKey,
		const std::string& workspaceDir,
		const PluginRuntimeSubagentMode runtimeSubagentMode,
		const bool pinHttpRoute,
		const bool pinChannel) {
		SetActiveRegistry(registry, cacheKey, workspaceDir, runtimeSubagentMode);

		if (pinHttpRoute) {
			PinHttpRouteRegistry(registry);
		}

		if (pinChannel) {
			PinChannelRegistry(registry);
		}

		RecordTransition("activate_runtime_registry");
	}

	void PluginRuntimeStateService::DeactivateRuntimeRegistry() {
		ReleasePinnedHttpRouteRegistry();
		ReleasePinnedChannelRegistry();
		SetActiveRegistry(
			nullptr,
			std::string{},
			std::string{},
			PluginRuntimeSubagentMode::Default);
		RecordTransition("deactivate_runtime_registry");
	}

	void PluginRuntimeStateService::PinHttpRouteRegistry(
		const std::vector<ExtensionManifest>* registry) {
		InstallSurfaceRegistry(m_state.httpRoute, registry, true);
		RecordTransition("pin_http_route_registry");
	}

	void PluginRuntimeStateService::ReleasePinnedHttpRouteRegistry(
		const std::vector<ExtensionManifest>* expectedRegistry) {
		if (expectedRegistry != nullptr &&
			m_state.httpRoute.registry != expectedRegistry) {
			return;
		}

		InstallSurfaceRegistry(m_state.httpRoute, m_state.activeRegistry, false);
		RecordTransition("release_http_route_registry");
	}

	void PluginRuntimeStateService::PinChannelRegistry(
		const std::vector<ExtensionManifest>* registry) {
		InstallSurfaceRegistry(m_state.channel, registry, true);
		RecordTransition("pin_channel_registry");
	}

	void PluginRuntimeStateService::ReleasePinnedChannelRegistry(
		const std::vector<ExtensionManifest>* expectedRegistry) {
		if (expectedRegistry != nullptr &&
			m_state.channel.registry != expectedRegistry) {
			return;
		}

		InstallSurfaceRegistry(m_state.channel, m_state.activeRegistry, false);
		RecordTransition("release_channel_registry");
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::GetActiveRegistry() const {
		return m_state.activeRegistry;
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::RequireActiveRegistry(
			const std::vector<ExtensionManifest>* fallbackRegistry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			const PluginRuntimeSubagentMode runtimeSubagentMode) {
		if (m_state.activeRegistry != nullptr) {
			return m_state.activeRegistry;
		}

		SetActiveRegistry(
			fallbackRegistry,
			cacheKey,
			workspaceDir,
			runtimeSubagentMode);
		return m_state.activeRegistry;
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::RequireHttpRouteRegistry(
			const std::vector<ExtensionManifest>* fallbackRegistry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			const PluginRuntimeSubagentMode runtimeSubagentMode) {
		const auto* existing = GetHttpRouteRegistry();
		if (existing != nullptr) {
			return existing;
		}

		const auto* ensured = RequireActiveRegistry(
			fallbackRegistry,
			cacheKey,
			workspaceDir,
			runtimeSubagentMode);
		InstallSurfaceRegistry(m_state.httpRoute, ensured, false);
		return GetHttpRouteRegistry();
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::RequireChannelRegistry(
			const std::vector<ExtensionManifest>* fallbackRegistry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			const PluginRuntimeSubagentMode runtimeSubagentMode) {
		const auto* existing = GetChannelRegistry();
		if (existing != nullptr) {
			return existing;
		}

		const auto* ensured = RequireActiveRegistry(
			fallbackRegistry,
			cacheKey,
			workspaceDir,
			runtimeSubagentMode);
		InstallSurfaceRegistry(m_state.channel, ensured, false);
		return GetChannelRegistry();
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::GetHttpRouteRegistry() const {
		if (m_state.httpRoute.registry != nullptr) {
			return m_state.httpRoute.registry;
		}

		return m_state.activeRegistry;
	}

	const std::vector<ExtensionManifest>*
		PluginRuntimeStateService::GetChannelRegistry() const {
		if (m_state.channel.registry != nullptr) {
			return m_state.channel.registry;
		}

		return m_state.activeRegistry;
	}

	std::uint64_t PluginRuntimeStateService::GetActiveVersion() const {
		return m_state.activeVersion;
	}

	std::uint64_t PluginRuntimeStateService::GetHttpRouteVersion() const {
		if (m_state.httpRoute.registry != nullptr) {
			return m_state.httpRoute.version;
		}

		return m_state.activeVersion;
	}

	std::uint64_t PluginRuntimeStateService::GetChannelVersion() const {
		if (m_state.channel.registry != nullptr) {
			return m_state.channel.version;
		}

		return m_state.activeVersion;
	}

	const std::string& PluginRuntimeStateService::GetCacheKey() const {
		return m_state.cacheKey;
	}

	const std::string& PluginRuntimeStateService::GetWorkspaceDir() const {
		return m_state.workspaceDir;
	}

	PluginRuntimeSubagentMode PluginRuntimeStateService::GetRuntimeSubagentMode() const {
		return m_state.runtimeSubagentMode;
	}

	void PluginRuntimeStateService::RecordImportedPluginId(
		const std::string& pluginId) {
		if (pluginId.empty()) {
			return;
		}

		m_state.importedPluginIds.insert(pluginId);
	}

	std::vector<std::string>
		PluginRuntimeStateService::ListImportedRuntimePluginIds() const {
		return std::vector<std::string>(
			m_state.importedPluginIds.begin(),
			m_state.importedPluginIds.end());
	}

	std::vector<PluginRuntimeCapabilityContract>
		PluginRuntimeStateService::ListCapabilityContracts() const {
		return {
			PluginRuntimeCapabilityContract{
				.capabilityId = "plugin-runtime.active-registry",
				.owner = "gateway/PluginRuntimeStateService",
				.version = "v1",
				.stability = "internal-stable",
				.description = "Active plugin runtime registry state contract",
			},
			PluginRuntimeCapabilityContract{
				.capabilityId = "plugin-runtime.surface-pinning",
				.owner = "gateway/PluginRuntimeStateService",
				.version = "v1",
				.stability = "internal-stable",
				.description = "Route/channel surface pin and release semantics",
			},
			PluginRuntimeCapabilityContract{
				.capabilityId = "plugin-runtime.import-trace",
				.owner = "gateway/PluginRuntimeStateService",
				.version = "v1",
				.stability = "internal-stable",
				.description = "Imported runtime plugin id trace visibility",
			},
			PluginRuntimeCapabilityContract{
				.capabilityId = "plugin-runtime.transition-history",
				.owner = "gateway/PluginRuntimeStateService",
				.version = "v1",
				.stability = "experimental",
				.description = "Runtime state transition telemetry history",
			},
		};
	}

	std::vector<PluginRuntimeTransitionEntry>
		PluginRuntimeStateService::GetTransitionHistory() const {
		return m_transitionHistory;
	}

	std::vector<PluginRuntimeTransitionEntry>
		PluginRuntimeStateService::ExportTransitionHistory() const {
		if (!m_transitionPolicy.exportEnabled) {
			return {};
		}

		return m_transitionHistory;
	}

	void PluginRuntimeStateService::SetTransitionPolicySettings(
		TransitionPolicySettings settings) {
		if (settings.historyLimit == 0) {
			settings.historyLimit = 1;
		}

		if (settings.historyLimit > 1024) {
			settings.historyLimit = 1024;
		}

		m_transitionPolicy = settings;
		if (m_transitionHistory.size() > m_transitionPolicy.historyLimit) {
			const std::size_t overflow =
				m_transitionHistory.size() - m_transitionPolicy.historyLimit;
			m_transitionHistory.erase(
				m_transitionHistory.begin(),
				m_transitionHistory.begin() +
				static_cast<std::ptrdiff_t>(overflow));
		}
	}

	PluginRuntimeStateService::TransitionPolicySettings
		PluginRuntimeStateService::GetTransitionPolicySettings() const {
		return m_transitionPolicy;
	}

	PluginRuntimeStateSnapshot PluginRuntimeStateService::Snapshot() const {
		return m_state;
	}

	void PluginRuntimeStateService::ResetForTest() {
		m_state.activeRegistry = nullptr;
		m_state.activeVersion += 1;
		InstallSurfaceRegistry(m_state.httpRoute, nullptr, false);
		InstallSurfaceRegistry(m_state.channel, nullptr, false);
		m_state.cacheKey.clear();
		m_state.workspaceDir.clear();
		m_state.runtimeSubagentMode = PluginRuntimeSubagentMode::Default;
		m_state.importedPluginIds.clear();
		m_transitionHistory.clear();
		m_transitionSequence = 0;
		RecordTransition("reset_for_test");
	}

} // namespace blazeclaw::gateway
