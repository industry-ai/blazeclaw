#pragma once

#include "SkillsCatalogService.h"

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct HookFrontmatter {
  std::wstring name;
  std::wstring description;
  std::wstring eventName;
  std::wstring handlerPath;
};

struct HookCatalogEntry {
  std::wstring skillName;
  std::filesystem::path hookFile;
  std::filesystem::path handlerFile;
  bool validMetadata = false;
  bool safeHandlerPath = false;
  bool handlerExists = false;
  std::vector<std::wstring> validationErrors;
  HookFrontmatter frontmatter;
};

struct HookCatalogDiagnostics {
  std::uint32_t hooksLoaded = 0;
  std::uint32_t invalidMetadataFiles = 0;
  std::uint32_t unsafeHandlerPaths = 0;
  std::uint32_t missingHandlerFiles = 0;
  std::vector<std::wstring> warnings;
};

struct HookCatalogSnapshot {
  std::vector<HookCatalogEntry> entries;
  HookCatalogDiagnostics diagnostics;
};

class HookCatalogService {
public:
  [[nodiscard]] HookCatalogSnapshot BuildSnapshot(
      const SkillsCatalogSnapshot& catalog) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static std::optional<HookFrontmatter> ParseFrontmatter(
      const std::wstring& hookContent,
      std::vector<std::wstring>& outValidationErrors);
};

} // namespace blazeclaw::core
