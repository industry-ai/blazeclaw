#include "pch.h"
#include "AgentsShellRuntimeService.h"

#include <algorithm>

namespace blazeclaw::core {

namespace {

std::string NormalizeAgentId(const std::string& value) {
  if (value.empty()) {
    return "default";
  }

  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if ((lowered >= 'a' && lowered <= 'z') ||
        (lowered >= '0' && lowered <= '9') ||
        lowered == '-' || lowered == '_') {
      normalized.push_back(lowered);
    }
  }

  return normalized.empty() ? "default" : normalized;
}

} // namespace

void AgentsShellRuntimeService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
}

bool AgentsShellRuntimeService::IsUnsafePath(const std::string& path) {
  if (path.find("..") != std::string::npos) {
    return true;
  }

  if (path.find(':') != std::string::npos) {
    return true;
  }

  return false;
}

ShellExecResult AgentsShellRuntimeService::Execute(
    const ShellExecRequest& request) {
  if (request.command.empty()) {
    return ShellExecResult{
      .executed = false,
      .status = "rejected",
      .stdoutText = {},
      .stderrText = "missing_command",
      .exitCode = 1,
      .processId = {},
      .approved = false,
    };
  }

  if (IsUnsafePath(request.cwd)) {
    return ShellExecResult{
      .executed = false,
      .status = "rejected",
      .stdoutText = {},
      .stderrText = "unsafe_cwd",
      .exitCode = 1,
      .processId = {},
      .approved = false,
    };
  }

  const std::string processId = "proc-" + std::to_string(m_processes.size() + 1);
  ShellProcessRecord record;
  record.processId = processId;
  record.agentId = NormalizeAgentId(request.agentId);
  record.cwd = request.cwd.empty() ? "." : request.cwd;
  record.command = request.command;
  record.status = "running";
  record.startedAtMs = 1735689800000 + m_processes.size();

  m_processes.insert_or_assign(processId, record);

  return ShellExecResult{
    .executed = true,
    .status = "running",
    .stdoutText = {},
    .stderrText = {},
    .exitCode = 0,
    .processId = processId,
    .approved = true,
  };
}

bool AgentsShellRuntimeService::AbortProcess(const std::string& processId) {
  const auto it = m_processes.find(processId);
  if (it == m_processes.end()) {
    return false;
  }

  it->second.status = "aborted";
  it->second.endedAtMs = it->second.startedAtMs + 1;
  return true;
}

bool AgentsShellRuntimeService::SendProcessKeys(
    const std::string& processId,
    const std::string& /*keys*/) {
  const auto it = m_processes.find(processId);
  if (it == m_processes.end()) {
    return false;
  }

  if (it->second.status != "running") {
    return false;
  }

  return true;
}

std::optional<ShellProcessRecord> AgentsShellRuntimeService::PollProcess(
    const std::string& processId) {
  const auto it = m_processes.find(processId);
  if (it == m_processes.end()) {
    return std::nullopt;
  }

  auto& run = it->second;
  ++run.pollCount;
  if (run.pollCount >= 2 && run.status == "running") {
    run.status = "completed";
    run.endedAtMs = run.startedAtMs + 2;
  }

  return run;
}

std::vector<ShellProcessRecord> AgentsShellRuntimeService::ListProcesses() const {
  std::vector<ShellProcessRecord> output;
  output.reserve(m_processes.size());
  for (const auto& [_, value] : m_processes) {
    output.push_back(value);
  }

  std::sort(
      output.begin(),
      output.end(),
      [](const ShellProcessRecord& left, const ShellProcessRecord& right) {
        return left.processId < right.processId;
      });

  return output;
}

bool AgentsShellRuntimeService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;
  AgentsShellRuntimeService service;
  service.Configure(config);

  const auto rejected = service.Execute(ShellExecRequest{
      .agentId = "alpha",
      .cwd = "../unsafe",
      .command = "echo bad",
      .timeoutMs = 1000,
  });
  if (rejected.executed) {
    outError = L"Fixture validation failed: expected unsafe cwd rejection.";
    return false;
  }

  const auto running = service.Execute(ShellExecRequest{
      .agentId = "alpha",
      .cwd = "workspace",
      .command = "echo ok",
      .timeoutMs = 1000,
  });
  if (!running.executed || running.processId.empty()) {
    outError = L"Fixture validation failed: expected process execution acceptance.";
    return false;
  }

  auto firstPoll = service.PollProcess(running.processId);
  if (!firstPoll.has_value() || firstPoll->status != "running") {
    outError = L"Fixture validation failed: expected first poll to remain running.";
    return false;
  }

  auto secondPoll = service.PollProcess(running.processId);
  if (!secondPoll.has_value() || secondPoll->status != "completed") {
    outError = L"Fixture validation failed: expected second poll to complete process.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
