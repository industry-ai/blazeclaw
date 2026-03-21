#pragma once

#include "SkillsCatalogService.h"

#include <string>

namespace blazeclaw::core {

struct SkillsWatchSnapshot {
  std::uint64_t version = 0;
  std::uint64_t fingerprint = 0;
  bool changed = false;
  bool watchEnabled = true;
  std::uint32_t debounceMs = 250;
  std::uint64_t timestampMs = 0;
  std::wstring reason;
};

class SkillsWatchService {
public:
  [[nodiscard]] SkillsWatchSnapshot Observe(
      const SkillsCatalogSnapshot& catalog,
      const blazeclaw::config::AppConfig& appConfig,
      bool forceRefresh,
      const std::wstring& reason);

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError);

private:
  [[nodiscard]] static std::uint64_t ComputeFingerprint(
      const SkillsCatalogSnapshot& catalog);

  std::uint64_t m_version = 0;
  std::uint64_t m_lastFingerprint = 0;
  std::uint64_t m_lastUpdateMs = 0;
  bool m_initialized = false;
};

} // namespace blazeclaw::core
