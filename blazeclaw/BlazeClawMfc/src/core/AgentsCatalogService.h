#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct AgentScopeEntry {
  std::wstring id;
  std::wstring name;
  std::filesystem::path workspaceDir;
  std::filesystem::path agentDir;
  std::wstring model;
  bool isDefault = false;
};

struct AgentScopeDiagnostics {
  std::vector<std::wstring> warnings;
  std::uint32_t duplicateDefaultAgents = 0;
};

struct AgentScopeSnapshot {
  std::wstring defaultAgentId;
  std::vector<AgentScopeEntry> entries;
  AgentScopeDiagnostics diagnostics;
};

class AgentsCatalogService {
public:
  [[nodiscard]] AgentScopeSnapshot BuildSnapshot(
      const std::filesystem::path& workspaceRoot,
      const blazeclaw::config::AppConfig& appConfig) const;

  [[nodiscard]] std::wstring ResolveAgentIdFromSessionKey(
      const std::wstring& sessionKey,
      const AgentScopeSnapshot& snapshot) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

  [[nodiscard]] static std::wstring NormalizeAgentId(
      const std::wstring& rawValue);
};

} // namespace blazeclaw::core
