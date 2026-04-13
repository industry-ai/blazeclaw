#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsSyncSnapshot {
		bool success = false;
		std::filesystem::path sandboxSkillsRoot;
		std::wstring destinationNamingMode;
		std::uint32_t copiedSkills = 0;
		std::uint32_t skippedSkills = 0;
		std::vector<std::wstring> warnings;
	};

	class SkillsSyncService {
	public:
		[[nodiscard]] SkillsSyncSnapshot SyncToSandbox(
			const std::filesystem::path& workspaceRoot,
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const blazeclaw::config::AppConfig& appConfig);

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError);

	private:
		[[nodiscard]] static std::filesystem::path ResolveSandboxRoot(
			const std::filesystem::path& workspaceRoot,
			std::vector<std::wstring>& outWarnings);
	};

} // namespace blazeclaw::core
