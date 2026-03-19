#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct SkillsPromptSnapshot {
  std::wstring prompt;
  std::vector<std::wstring> includedSkills;
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
      const std::optional<std::vector<std::wstring>>& skillFilter = std::nullopt) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

  [[nodiscard]] static std::optional<std::vector<std::wstring>> NormalizeFilter(
      const std::optional<std::vector<std::wstring>>& input);
};

} // namespace blazeclaw::core
