#pragma once

#include "SkillsCatalogService.h"
#include "SkillsContracts.h"

#include <functional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	using SkillsRemoteEligibilityContext = SkillsRemoteEligibilityContract;

	struct SkillsEligibilityEntry {
		std::wstring skillName;
		std::wstring skillKey;
		bool eligible = false;
		bool disabled = false;
		bool blockedByAllowlist = false;
		bool disableModelInvocation = false;
		bool userInvocable = true;
		std::vector<std::wstring> missingOs;
		std::vector<std::wstring> missingBins;
		std::vector<std::wstring> missingAnyBins;
		std::vector<std::wstring> missingEnv;
		std::vector<std::wstring> missingConfig;
	};

	struct SkillsEligibilitySnapshot {
		std::vector<SkillsEligibilityEntry> entries;
		std::uint32_t eligibleCount = 0;
		std::uint32_t disabledCount = 0;
		std::uint32_t blockedByAllowlistCount = 0;
		std::uint32_t missingRequirementsCount = 0;
		std::uint32_t strictEntryResolutionModeCount = 0;
		std::uint32_t compatEntryResolutionModeCount = 0;
		std::uint32_t configResolvedByKeyCount = 0;
		std::uint32_t configResolvedByNameFallbackCount = 0;
		std::uint32_t allowlistRawCount = 0;
		std::uint32_t allowlistNormalizedCount = 0;
		std::uint32_t remoteEligibilityEnabledCount = 0;
		std::uint32_t remotePlatformSatisfiedCount = 0;
		std::uint32_t remoteBinSatisfiedCount = 0;
		std::uint32_t remoteAnyBinSatisfiedCount = 0;
		std::uint32_t alwaysBypassCount = 0;
	};

	class SkillsEligibilityService {
	public:
		[[nodiscard]] SkillsEligibilitySnapshot Evaluate(
			const SkillsCatalogSnapshot& catalog,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] SkillsEligibilitySnapshot Evaluate(
			const SkillsCatalogSnapshot& catalog,
			const blazeclaw::config::AppConfig& appConfig,
			const SkillsRemoteEligibilityContext* remoteContext) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;

	private:
		[[nodiscard]] static std::wstring ResolveSkillKey(
			const SkillsCatalogEntry& entry);
	};

} // namespace blazeclaw::core
