#pragma once

#include "AgentsCatalogService.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct AgentWorkspaceLifecycleState {
		bool bootstrapSeeded = false;
		bool setupCompleted = false;
		bool legacyOnboardingField = false;
		bool bootstrapFilePresent = false;
		bool bootstrapPending = false;
	};

	struct AgentWorkspaceExtraBootstrapDiagnostic {
		std::filesystem::path path;
		std::wstring reason;
		std::wstring detail;
	};

	struct AgentWorkspaceEntry {
		std::wstring agentId;
		std::filesystem::path workspaceDir;
		bool onboardingCompleted = false;
		AgentWorkspaceLifecycleState lifecycle;
		std::vector<std::wstring> missingBootstrapFiles;
		std::vector<AgentWorkspaceExtraBootstrapDiagnostic> extraBootstrapDiagnostics;
	};

	struct AgentsWorkspaceSnapshot {
		std::vector<AgentWorkspaceEntry> entries;
		std::uint32_t onboardingCompletedCount = 0;
		std::uint32_t bootstrapSeededCount = 0;
		std::uint32_t bootstrapPendingCount = 0;
		std::uint32_t legacyOnboardingFieldCount = 0;
		std::uint32_t extraBootstrapDiagnosticCount = 0;
		std::uint32_t extraBootstrapInvalidFilenameCount = 0;
		std::uint32_t extraBootstrapMissingCount = 0;
		std::uint32_t extraBootstrapSecurityCount = 0;
		std::uint32_t extraBootstrapIoCount = 0;
		std::vector<std::wstring> warnings;
	};

	class AgentsWorkspaceService {
	public:
		[[nodiscard]] AgentsWorkspaceSnapshot BuildSnapshot(
			const AgentScopeSnapshot& scopeSnapshot) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;

	private:
		[[nodiscard]] static bool IsOnboardingCompleted(
			const std::filesystem::path& workspaceDir);
	};

} // namespace blazeclaw::core
