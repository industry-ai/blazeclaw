#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct SkillScanFinding {
  std::wstring skillName;
  std::wstring file;
  std::wstring ruleId;
  std::wstring severity;
  std::wstring evidence;
};

struct SkillSecurityScanSnapshot {
  std::vector<SkillScanFinding> findings;
  std::uint32_t infoCount = 0;
  std::uint32_t warnCount = 0;
  std::uint32_t criticalCount = 0;
  std::uint32_t scannedFileCount = 0;
};

class SkillSecurityScanService {
public:
  [[nodiscard]] SkillSecurityScanSnapshot BuildSnapshot(
      const SkillsCatalogSnapshot& catalog,
      const SkillsEligibilitySnapshot& eligibility,
      const blazeclaw::config::AppConfig& appConfig) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;
};

} // namespace blazeclaw::core
