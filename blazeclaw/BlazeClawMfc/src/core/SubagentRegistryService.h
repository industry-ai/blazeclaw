#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

enum class SubagentRunStatus {
  Running,
  Completed,
  Failed,
  Timeout,
};

struct SubagentRunRecord {
  std::string runId;
  std::string parentSessionId;
  std::string childSessionId;
  std::string requesterAgentId;
  std::string targetAgentId;
  std::uint32_t depth = 0;
  bool expectsCompletionMessage = false;
  bool announcePending = false;
  std::uint32_t announceRetryCount = 0;
  bool orphaned = false;
  SubagentRunStatus status = SubagentRunStatus::Running;
  std::uint64_t startedAtMs = 0;
  std::optional<std::uint64_t> endedAtMs;
  std::string summary;
};

struct SubagentRegistrySnapshot {
  std::vector<SubagentRunRecord> runs;
  std::size_t activeRuns = 0;
  std::size_t pendingAnnounce = 0;
  std::size_t orphanedRuns = 0;
  std::size_t timeoutRuns = 0;
};

struct SubagentSpawnRequest {
  std::string requesterAgentId;
  std::string targetAgentId;
  std::string parentSessionId;
  std::string childSessionId;
  bool expectsCompletionMessage = true;
};

struct SubagentSpawnResult {
  bool accepted = false;
  std::string runId;
  std::string reason;
};

class SubagentRegistryService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);
  void Initialize(const std::filesystem::path& workspaceRoot);

  [[nodiscard]] SubagentSpawnResult Spawn(const SubagentSpawnRequest& request);

  [[nodiscard]] bool CompleteRun(
      const std::string& runId,
      SubagentRunStatus status,
      const std::string& summary,
      std::uint64_t endedAtMs);

  void ProcessAnnounceQueue(std::uint64_t nowMs);
  void ReconcileOrphans(const std::vector<std::string>& activeSessionIds);

  [[nodiscard]] SubagentRegistrySnapshot Snapshot() const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] bool IsAllowedTarget(
      const std::string& requesterAgentId,
      const std::string& targetAgentId) const;

  [[nodiscard]] std::uint32_t ResolveMaxDepth(
      const std::string& requesterAgentId) const;

  [[nodiscard]] std::uint32_t ResolveChildDepth(
      const std::string& parentSessionId) const;

  [[nodiscard]] std::filesystem::path PersistencePath() const;

  void LoadState();
  void PersistState() const;

  static std::string NormalizeAgentId(const std::string& value);
  static std::string StatusToString(SubagentRunStatus status);
  static SubagentRunStatus StatusFromString(const std::string& value);

  blazeclaw::config::AppConfig m_config;
  std::filesystem::path m_workspaceRoot;
  std::unordered_map<std::string, SubagentRunRecord> m_runsById;
};

} // namespace blazeclaw::core
