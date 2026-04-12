#pragma once

#include "SkillsCatalogService.h"
#include "SkillsCommandService.h"
#include "SkillsEnvOverrideService.h"
#include "SkillsEligibilityService.h"
#include "SkillsInstallService.h"
#include "SkillsPromptService.h"
#include "SkillsSyncService.h"
#include "SkillsWatchService.h"
#include "SkillSecurityScanService.h"

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsRunSnapshotSkill {
		std::wstring name;
		std::wstring primaryEnv;
		std::vector<std::wstring> requiredEnv;
	};

	struct SkillsRunSnapshot {
		std::wstring prompt;
		std::vector<SkillsRunSnapshotSkill> skills;
		std::optional<std::vector<std::wstring>> skillFilter;
		std::vector<std::wstring> resolvedSkills;
		std::uint64_t version = 0;
	};

	struct SkillsRefreshResult {
		SkillsCatalogSnapshot catalog;
		SkillsEligibilitySnapshot eligibility;
		SkillsPromptSnapshot prompt;
		SkillsCommandSnapshot commands;
		SkillsSyncSnapshot sync;
		SkillsEnvOverrideSnapshot envOverrides;
		SkillsInstallSnapshot install;
		SkillSecurityScanSnapshot securityScan;
		SkillsWatchSnapshot watch;
		SkillsRunSnapshot runSnapshot;
	};

	class SkillsFacade {
	public:
		[[nodiscard]] SkillsInstallPreferences ResolveInstallPreferences(
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] SkillsRunSnapshot BuildRunSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const SkillsPromptSnapshot& prompt,
			const SkillsWatchSnapshot& watch,
			const std::optional<std::vector<std::wstring>>& skillFilter = std::nullopt) const;

		[[nodiscard]] std::wstring ResolvePromptForRun(
			const SkillsRunSnapshot* runSnapshot,
			const SkillsPromptSnapshot* promptSnapshot,
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const blazeclaw::config::AppConfig& appConfig,
			const std::optional<std::vector<std::wstring>>& skillFilter,
			bool enableSelfEvolvingPromptFallback,
			const SkillsPromptService& promptService) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError,
			const SkillsPromptService& promptService) const;

		[[nodiscard]] SkillsRefreshResult RefreshSkillsState(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig,
			bool forceRefresh,
			const std::wstring& reason,
			bool enableSelfEvolvingPromptFallback,
			SkillsCatalogService& catalogService,
			SkillsEligibilityService& eligibilityService,
			SkillsPromptService& promptService,
			SkillsCommandService& commandService,
			SkillsSyncService& syncService,
			SkillsEnvOverrideService& envOverrideService,
			SkillsInstallService& installService,
			SkillSecurityScanService& securityScanService,
			SkillsWatchService& watchService) const;
	};

} // namespace blazeclaw::core
