#include "pch.h"
#include "PluginRuntimeStateService.h"

namespace blazeclaw::gateway {

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
	}

	void PluginRuntimeStateService::PinHttpRouteRegistry(
		const std::vector<ExtensionManifest>* registry) {
		InstallSurfaceRegistry(m_state.httpRoute, registry, true);
	}

	void PluginRuntimeStateService::ReleasePinnedHttpRouteRegistry(
		const std::vector<ExtensionManifest>* expectedRegistry) {
		if (expectedRegistry != nullptr &&
			m_state.httpRoute.registry != expectedRegistry) {
			return;
		}

		InstallSurfaceRegistry(m_state.httpRoute, m_state.activeRegistry, false);
	}

	void PluginRuntimeStateService::PinChannelRegistry(
		const std::vector<ExtensionManifest>* registry) {
		InstallSurfaceRegistry(m_state.channel, registry, true);
	}

	void PluginRuntimeStateService::ReleasePinnedChannelRegistry(
		const std::vector<ExtensionManifest>* expectedRegistry) {
		if (expectedRegistry != nullptr &&
			m_state.channel.registry != expectedRegistry) {
			return;
		}

		InstallSurfaceRegistry(m_state.channel, m_state.activeRegistry, false);
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
	}

} // namespace blazeclaw::gateway
