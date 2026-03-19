#pragma once

#include "AgentsCatalogService.h"
#include "../config/ConfigModels.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct SandboxEntry {
  std::string agentId;
  bool enabled = false;
  std::string runtime;
  std::filesystem::path workspaceMirror;
  bool browserEnabled = false;
  bool allowHostNetwork = false;
};

struct SandboxSnapshot {
  std::vector<SandboxEntry> entries;
  std::size_t enabledCount = 0;
  std::size_t browserEnabledCount = 0;
};

struct SandboxPolicyDecision {
  bool allowed = false;
  std::string reason;
};

class AgentsSandboxService {
public:
  [[nodiscard]] SandboxSnapshot BuildSnapshot(
      const AgentScopeSnapshot& scopeSnapshot,
      const blazeclaw::config::AppConfig& appConfig) const;

  [[nodiscard]] SandboxPolicyDecision EvaluateToolPolicy(
      const SandboxSnapshot& snapshot,
      const std::string& agentId,
      const std::string& toolName) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static std::string ToNarrow(const std::wstring& value);
  [[nodiscard]] static std::string NormalizeId(const std::string& value);
};

} // namespace blazeclaw::core
