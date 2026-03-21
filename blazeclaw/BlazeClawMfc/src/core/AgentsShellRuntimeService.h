#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

struct ShellExecRequest {
  std::string agentId;
  std::string cwd;
  std::string command;
  std::uint32_t timeoutMs = 30000;
};

struct ShellExecResult {
  bool executed = false;
  std::string status;
  std::string stdoutText;
  std::string stderrText;
  std::uint32_t exitCode = 0;
  std::string processId;
  bool approved = false;
};

struct ShellProcessRecord {
  std::string processId;
  std::string agentId;
  std::string cwd;
  std::string command;
  std::string status;
  std::uint64_t startedAtMs = 0;
  std::optional<std::uint64_t> endedAtMs;
  std::uint32_t pollCount = 0;
};

class AgentsShellRuntimeService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);

  [[nodiscard]] ShellExecResult Execute(const ShellExecRequest& request);

  [[nodiscard]] bool AbortProcess(const std::string& processId);

  [[nodiscard]] bool SendProcessKeys(
      const std::string& processId,
      const std::string& keys);

  [[nodiscard]] std::optional<ShellProcessRecord> PollProcess(
      const std::string& processId);

  [[nodiscard]] std::vector<ShellProcessRecord> ListProcesses() const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static bool IsUnsafePath(const std::string& path);

  blazeclaw::config::AppConfig m_config;
  std::unordered_map<std::string, ShellProcessRecord> m_processes;
};

} // namespace blazeclaw::core
