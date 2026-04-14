#pragma once

#include "ExtensionLifecycleManager.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

	enum class PluginRuntimeTransitionSeverity {
		Info,
		Warn,
		Error,
	};

	enum class PluginRuntimeLifecyclePhase {
		Activation,
		SteadyState,
		Mutation,
		Deactivation,
		Maintenance,
	};

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

	struct PluginRuntimeCapabilityContract {
		std::string capabilityId;
		std::string owner;
		std::string version;
		std::string stability;
		std::string description;
	};

	struct PluginRuntimeTransitionEntry {
		std::uint64_t sequence = 0;
		std::uint64_t timestampMs = 0;
		std::string action;
		PluginRuntimeTransitionSeverity severity =
			PluginRuntimeTransitionSeverity::Info;
		PluginRuntimeLifecyclePhase lifecyclePhase =
			PluginRuntimeLifecyclePhase::SteadyState;
		std::uint64_t activeVersion = 0;
		std::uint64_t httpRouteVersion = 0;
		std::uint64_t channelVersion = 0;
		std::size_t activeRegistryCount = 0;
		bool httpRoutePinned = false;
		bool channelPinned = false;
		std::string cacheKey;
		std::string workspaceDir;
		PluginRuntimeSubagentMode runtimeSubagentMode =
			PluginRuntimeSubagentMode::Default;
	};

	class PluginRuntimeStateService {
	public:
		PluginRuntimeStateService();

		struct TransitionPolicySettings {
			std::size_t historyLimit = 128;
			bool exportEnabled = false;
		};

		void SetActiveRegistry(
			const std::vector<ExtensionManifest>* registry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			PluginRuntimeSubagentMode runtimeSubagentMode);

		void ActivateRuntimeRegistry(
			const std::vector<ExtensionManifest>* registry,
			const std::string& cacheKey,
			const std::string& workspaceDir,
			PluginRuntimeSubagentMode runtimeSubagentMode,
			bool pinHttpRoute,
			bool pinChannel);

		void DeactivateRuntimeRegistry();

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
		[[nodiscard]] std::vector<PluginRuntimeCapabilityContract>
			ListCapabilityContracts() const;
		[[nodiscard]] std::vector<PluginRuntimeTransitionEntry>
			GetTransitionHistory() const;
		[[nodiscard]] std::vector<PluginRuntimeTransitionEntry>
			ExportTransitionHistory() const;

		void SetTransitionPolicySettings(TransitionPolicySettings settings);
		[[nodiscard]] TransitionPolicySettings GetTransitionPolicySettings() const;

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
		[[nodiscard]] static PluginRuntimeTransitionSeverity
			ResolveTransitionSeverity(const std::string& action);
		[[nodiscard]] static PluginRuntimeLifecyclePhase
			ResolveTransitionLifecyclePhase(const std::string& action);

		void RecordTransition(const std::string& action);
		[[nodiscard]] static std::uint64_t CurrentEpochMs();

		PluginRuntimeStateSnapshot m_state;
		TransitionPolicySettings m_transitionPolicy;
		std::vector<PluginRuntimeTransitionEntry> m_transitionHistory;
		std::uint64_t m_transitionSequence = 0;
	};

} // namespace blazeclaw::gateway
