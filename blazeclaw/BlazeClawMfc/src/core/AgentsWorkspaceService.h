#pragma once

#include "AgentsCatalogService.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct AgentWorkspaceEntry {
  std::wstring agentId;
  std::filesystem::path workspaceDir;
  bool onboardingCompleted = false;
  std::vector<std::wstring> missingBootstrapFiles;
};

struct AgentsWorkspaceSnapshot {
  std::vector<AgentWorkspaceEntry> entries;
  std::uint32_t onboardingCompletedCount = 0;
  std::vector<std::wstring> warnings;
};

class AgentsWorkspaceService {
public:
  [[nodiscard]] AgentsWorkspaceSnapshot BuildSnapshot(
      const AgentScopeSnapshot& scopeSnapshot) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static bool IsOnboardingCompleted(
      const std::filesystem::path& workspaceDir);
};

} // namespace blazeclaw::core
