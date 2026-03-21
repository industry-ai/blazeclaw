#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace blazeclaw::core {

struct EmbeddedRunRequest {
  std::string sessionId;
  std::string agentId;
  std::string message;
};

struct EmbeddedRunResult {
  bool accepted = false;
  std::string runId;
  std::string status;
  std::string reason;
  std::uint64_t startedAtMs = 0;
};

struct EmbeddedRunRecord {
  std::string runId;
  std::string sessionId;
  std::string agentId;
  std::string message;
  std::string status;
  std::uint64_t startedAtMs = 0;
  std::optional<std::uint64_t> completedAtMs;
};

class PiEmbeddedService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);

  [[nodiscard]] EmbeddedRunResult QueueRun(const EmbeddedRunRequest& request);

  [[nodiscard]] bool CompleteRun(
      const std::string& runId,
      const std::string& status,
      std::uint64_t completedAtMs);

  [[nodiscard]] std::size_t ActiveRuns() const;

  [[nodiscard]] std::optional<EmbeddedRunRecord> GetRun(
      const std::string& runId) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  blazeclaw::config::AppConfig m_config;
  std::unordered_map<std::string, EmbeddedRunRecord> m_runsById;
};

} // namespace blazeclaw::core
