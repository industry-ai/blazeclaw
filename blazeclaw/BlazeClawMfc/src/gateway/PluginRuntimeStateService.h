#pragma once

#include "ExtensionLifecycleManager.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

	enum class PluginRuntimeSubagentMode {
		Default,
		Explicit,
		GatewayBindable,
	};

	struct PluginRuntimeSurfaceState {
		const std::vector<ExtensionManifest>* registry = nullptr;
		bool pinned = false;
		std::uint64_t version = 0;
	};

	struct PluginRuntimeStateSnapshot {
		const std::vector<ExtensionManifest>* activeRegistry = nullptr;
		std::uint64_t activeVersion = 0;
		PluginRuntimeSurfaceState httpRoute;
		PluginRuntimeSurfaceState channel;
		std::string cacheKey;
		std::string workspaceDir;
		PluginRuntimeSubagentMode runtimeSubagentMode =
			PluginRuntimeSubagentMode::Default;
		std::set<std::string> importedPluginIds;
	};

	class PluginRuntimeStateService {
	public:
		void SetActiveRegistry(
			const std::vector<ExtensionManifest>* registry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			PluginRuntimeSubagentMode runtimeSubagentMode);

		void PinHttpRouteRegistry(const std::vector<ExtensionManifest>* registry);
		void ReleasePinnedHttpRouteRegistry(
			const std::vector<ExtensionManifest>* expectedRegistry = nullptr);

		void PinChannelRegistry(const std::vector<ExtensionManifest>* registry);
		void ReleasePinnedChannelRegistry(
			const std::vector<ExtensionManifest>* expectedRegistry = nullptr);

		[[nodiscard]] const std::vector<ExtensionManifest>* GetActiveRegistry() const;
		[[nodiscard]] const std::vector<ExtensionManifest>* GetHttpRouteRegistry() const;
		[[nodiscard]] const std::vector<ExtensionManifest>* GetChannelRegistry() const;
		[[nodiscard]] const std::vector<ExtensionManifest>*
			RequireActiveRegistry(
				const std::vector<ExtensionManifest>* fallbackRegistry,
				const std::string& cacheKey,
				const std::string& workspaceDir,
				PluginRuntimeSubagentMode runtimeSubagentMode);
		[[nodiscard]] const std::vector<ExtensionManifest>*
			RequireHttpRouteRegistry(
				const std::vector<ExtensionManifest>* fallbackRegistry,
				const std::string& cacheKey,
				const std::string& workspaceDir,
				PluginRuntimeSubagentMode runtimeSubagentMode);
		[[nodiscard]] const std::vector<ExtensionManifest>*
			RequireChannelRegistry(
				const std::vector<ExtensionManifest>* fallbackRegistry,
				const std::string& cacheKey,
				const std::string& workspaceDir,
				PluginRuntimeSubagentMode runtimeSubagentMode);
		[[nodiscard]] std::uint64_t GetActiveVersion() const;
		[[nodiscard]] std::uint64_t GetHttpRouteVersion() const;
		[[nodiscard]] std::uint64_t GetChannelVersion() const;
		[[nodiscard]] const std::string& GetCacheKey() const;
		[[nodiscard]] const std::string& GetWorkspaceDir() const;
		[[nodiscard]] PluginRuntimeSubagentMode GetRuntimeSubagentMode() const;
		void RecordImportedPluginId(const std::string& pluginId);
		[[nodiscard]] std::vector<std::string> ListImportedRuntimePluginIds() const;

		[[nodiscard]] PluginRuntimeStateSnapshot Snapshot() const;
		void ResetForTest();

	private:
		void InstallSurfaceRegistry(
			PluginRuntimeSurfaceState& surface,
			const std::vector<ExtensionManifest>* registry,
			bool pinned);

		void SyncSurface(
			PluginRuntimeSurfaceState& surface,
			const std::vector<ExtensionManifest>* registry,
			bool refreshVersion = false);

		PluginRuntimeStateSnapshot m_state;
	};

} // namespace blazeclaw::gateway
