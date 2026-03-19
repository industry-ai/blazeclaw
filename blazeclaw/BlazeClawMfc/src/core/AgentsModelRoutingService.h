#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

struct ModelFailoverRecord {
  std::string modelId;
  std::string reason;
  std::uint64_t timestampMs = 0;
};

struct ModelRoutingSnapshot {
  std::string primaryModel;
  std::string fallbackModel;
  std::vector<std::string> allowedModels;
  std::unordered_map<std::string, std::string> aliases;
  std::vector<ModelFailoverRecord> failoverHistory;
};

struct ModelSelectionResult {
  std::string selectedModel;
  std::string routeReason;
  std::size_t failoverAttempt = 0;
};

class AgentsModelRoutingService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);

  [[nodiscard]] ModelSelectionResult SelectModel(
      const std::string& requestedModel,
      const std::string& routeReason);

  void RecordFailover(
      const std::string& modelId,
      const std::string& reason,
      std::uint64_t timestampMs);

  [[nodiscard]] ModelRoutingSnapshot Snapshot() const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] std::string ResolveAlias(const std::string& modelId) const;
  [[nodiscard]] bool IsAllowedModel(const std::string& modelId) const;

  blazeclaw::config::AppConfig m_config;
  std::vector<ModelFailoverRecord> m_failoverHistory;
  std::size_t m_failoverAttempts = 0;
};

} // namespace blazeclaw::core
