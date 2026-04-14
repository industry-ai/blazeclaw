#pragma once

#include "SkillsCatalogService.h"
#include "SkillsContracts.h"
#include "SkillsEligibilityService.h"

#include <string>
#include <vector>

namespace blazeclaw::core {

	using SkillsInstallSpec = SkillInstallSpec;

	struct SkillsInstallPreferences {
		bool preferBrew = true;
		std::wstring nodeManager = L"npm";
	};

	struct SkillsInstallPlanEntry {
		std::wstring skillName;
		std::wstring kind;
		std::wstring label;
		std::wstring command;
		bool executable = false;
		std::wstring reason;
	};

	struct SkillsInstallSnapshot {
		std::vector<SkillsInstallPlanEntry> entries;
		std::uint32_t executableCount = 0;
		std::uint32_t blockedCount = 0;
	};

	class SkillsInstallService {
	public:
		[[nodiscard]] SkillsInstallSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] SkillsInstallSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const blazeclaw::config::AppConfig& appConfig,
			const SkillsInstallPreferences& installPreferences) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;
	};

} // namespace blazeclaw::core
