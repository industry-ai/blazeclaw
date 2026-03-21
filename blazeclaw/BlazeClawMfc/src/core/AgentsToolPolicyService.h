#pragma once

#include "AgentsCatalogService.h"
#include "../config/ConfigModels.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct AgentToolPolicyEntry {
  std::string agentId;
  std::string profile;
  std::vector<std::string> allowedTools;
  std::vector<std::string> deniedTools;
  std::vector<std::string> ownerOnlyTools;
  std::vector<std::string> resolvedTools;
};

struct AgentsToolPolicySnapshot {
  std::vector<AgentToolPolicyEntry> entries;
  std::vector<std::string> coreCatalog;
};

class AgentsToolPolicyService {
public:
  [[nodiscard]] AgentsToolPolicySnapshot BuildSnapshot(
      const AgentScopeSnapshot& scopeSnapshot,
      const blazeclaw::config::AppConfig& appConfig) const;

  [[nodiscard]] std::vector<std::string> ResolveToolsForAgent(
      const AgentsToolPolicySnapshot& snapshot,
      const std::string& agentId,
      bool senderIsOwner) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static std::vector<std::string> CoreToolsForProfile(
      const std::string& profile,
      const std::vector<std::string>& catalog);

  [[nodiscard]] static std::vector<std::string> NormalizeToolList(
      const std::vector<std::wstring>& values);

  [[nodiscard]] static std::string NormalizeAgentId(const std::string& value);
};

} // namespace blazeclaw::core
