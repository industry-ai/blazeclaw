#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <string>
#include <vector>

namespace blazeclaw::core {

struct SkillsEnvOverrideItem {
  std::wstring key;
  std::wstring value;
  std::wstring skillName;
  bool blocked = false;
  std::wstring blockedReason;
};

struct SkillsEnvOverrideSnapshot {
  std::vector<SkillsEnvOverrideItem> items;
  std::uint32_t allowedCount = 0;
  std::uint32_t blockedCount = 0;
};

class SkillsEnvOverrideService {
public:
  [[nodiscard]] SkillsEnvOverrideSnapshot BuildSnapshot(
      const SkillsCatalogSnapshot& catalog,
      const SkillsEligibilitySnapshot& eligibility,
      const blazeclaw::config::AppConfig& appConfig) const;

  void Apply(const SkillsEnvOverrideSnapshot& snapshot);
  void RevertAll();

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError);

private:
  struct ActiveEnvValue {
    std::optional<std::wstring> baseline;
    std::wstring managedValue;
    std::size_t refCount = 0;
  };

  std::map<std::wstring, ActiveEnvValue> m_active;
  std::vector<std::wstring> m_lastApplied;
};

} // namespace blazeclaw::core
