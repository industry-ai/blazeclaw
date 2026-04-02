#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsPromptSnapshot {
		struct PlannerSkillContext {
			std::wstring skillName;
			std::wstring capability;
			std::wstring preconditions;
			std::wstring sideEffects;
			std::wstring commandToolName;
		};

		std::wstring prompt;
		std::vector<std::wstring> includedSkills;
		std::vector<PlannerSkillContext> plannerContext;
		std::optional<std::vector<std::wstring>> filter;
		bool truncated = false;
		std::uint32_t totalEligible = 0;
		std::uint32_t includedCount = 0;
		std::uint32_t promptChars = 0;
	};

	class SkillsPromptService {
	public:
		[[nodiscard]] SkillsPromptSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const blazeclaw::config::AppConfig& appConfig,
			const std::optional<std::vector<std::wstring>>& skillFilter = std::nullopt,
			bool enableSelfEvolvingPromptFallback = false) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;

		[[nodiscard]] static std::optional<std::vector<std::wstring>> NormalizeFilter(
			const std::optional<std::vector<std::wstring>>& input);
	};

} // namespace blazeclaw::core
