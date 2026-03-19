#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>

namespace blazeclaw::core {

struct AcpSpawnRequest {
  std::string requesterSessionId;
  std::string requesterAgentId;
  std::string targetAgentId;
  bool threadRequested = false;
  bool requesterSandboxed = false;
};

struct AcpSpawnDecision {
  bool allowed = false;
  std::string resolvedAgentId;
  std::string mode;
  std::string reason;
};

class AcpSpawnService {
public:
  [[nodiscard]] AcpSpawnDecision Evaluate(
      const blazeclaw::config::AppConfig& appConfig,
      const AcpSpawnRequest& request) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;
};

} // namespace blazeclaw::core
