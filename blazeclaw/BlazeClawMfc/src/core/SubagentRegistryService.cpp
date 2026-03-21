#include "pch.h"
#include "SubagentRegistryService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace blazeclaw::core {

namespace {

std::string Trim(const std::string& value) {
  std::size_t start = 0;
  std::size_t end = value.size();

  while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }

  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }

  return value.substr(start, end - start);
}

std::vector<std::string> Split(
    const std::string& value,
    const char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto next = value.find(delimiter, start);
    if (next == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }

    parts.push_back(value.substr(start, next - start));
    start = next + 1;
  }

  return parts;
}

std::string ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());

  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::uint64_t ParseUInt64(const std::string& value, const std::uint64_t fallback) {
  try {
    return static_cast<std::uint64_t>(std::stoull(value));
  } catch (...) {
    return fallback;
  }
}

} // namespace

void SubagentRegistryService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
}

void SubagentRegistryService::Initialize(
    const std::filesystem::path& workspaceRoot) {
  m_workspaceRoot = workspaceRoot;
  LoadState();
}

SubagentSpawnResult SubagentRegistryService::Spawn(
    const SubagentSpawnRequest& request) {
  const std::string requesterAgent = NormalizeAgentId(request.requesterAgentId);
  const std::string targetAgent = NormalizeAgentId(request.targetAgentId);

  if (!IsAllowedTarget(requesterAgent, targetAgent)) {
    return SubagentSpawnResult{
      .accepted = false,
      .runId = {},
      .reason = "target_not_allowed",
    };
  }

  const std::uint32_t depth = ResolveChildDepth(request.parentSessionId);
  const std::uint32_t maxDepth = ResolveMaxDepth(requesterAgent);
  if (depth > maxDepth) {
    return SubagentSpawnResult{
      .accepted = false,
      .runId = {},
      .reason = "depth_exceeded",
    };
  }

  const std::string runId =
      "subrun-" + std::to_string(m_runsById.size() + 1) +
      "-" + requesterAgent + "-" + targetAgent;

  SubagentRunRecord record;
  record.runId = runId;
  record.parentSessionId = request.parentSessionId;
  record.childSessionId = request.childSessionId;
  record.requesterAgentId = requesterAgent;
  record.targetAgentId = targetAgent;
  record.depth = depth;
  record.expectsCompletionMessage = request.expectsCompletionMessage;
  record.announcePending = request.expectsCompletionMessage;
  record.status = SubagentRunStatus::Running;
  record.startedAtMs = 1735689600000 + m_runsById.size();

  m_runsById.insert_or_assign(runId, record);
  PersistState();

  return SubagentSpawnResult{
    .accepted = true,
    .runId = runId,
    .reason = "accepted",
  };
}

bool SubagentRegistryService::CompleteRun(
    const std::string& runId,
    const SubagentRunStatus status,
    const std::string& summary,
    const std::uint64_t endedAtMs) {
  const auto it = m_runsById.find(runId);
  if (it == m_runsById.end()) {
    return false;
  }

  it->second.status = status;
  it->second.summary = summary;
  it->second.endedAtMs = endedAtMs;
  it->second.announcePending = it->second.expectsCompletionMessage;
  PersistState();
  return true;
}

void SubagentRegistryService::ProcessAnnounceQueue(const std::uint64_t nowMs) {
  constexpr std::uint32_t kMaxAnnounceRetries = 3;
  constexpr std::uint64_t kCleanupRetentionMs = 5 * 60 * 1000;

  std::vector<std::string> cleanupIds;
  for (auto& [runId, run] : m_runsById) {
    if (run.announcePending) {
      if (run.announceRetryCount >= kMaxAnnounceRetries) {
        run.announcePending = false;
      } else {
        ++run.announceRetryCount;
        if (run.announceRetryCount >= 1) {
          run.announcePending = false;
        }
      }
    }

    if (run.endedAtMs.has_value() &&
        nowMs > run.endedAtMs.value() &&
        (nowMs - run.endedAtMs.value()) > kCleanupRetentionMs &&
        !run.announcePending) {
      cleanupIds.push_back(runId);
    }
  }

  for (const auto& runId : cleanupIds) {
    m_runsById.erase(runId);
  }

  PersistState();
}

void SubagentRegistryService::ReconcileOrphans(
    const std::vector<std::string>& activeSessionIds) {
  for (auto& [_, run] : m_runsById) {
    if (run.endedAtMs.has_value()) {
      continue;
    }

    const bool childAlive = std::find(
        activeSessionIds.begin(),
        activeSessionIds.end(),
        run.childSessionId) != activeSessionIds.end();

    if (childAlive) {
      continue;
    }

    run.orphaned = true;
    run.status = SubagentRunStatus::Failed;
    run.summary = "orphaned_subagent_run";
    run.endedAtMs = run.startedAtMs + 1;
    run.announcePending = false;
  }

  PersistState();
}

SubagentRegistrySnapshot SubagentRegistryService::Snapshot() const {
  SubagentRegistrySnapshot snapshot;
  snapshot.runs.reserve(m_runsById.size());

  for (const auto& [_, run] : m_runsById) {
    snapshot.runs.push_back(run);
    if (!run.endedAtMs.has_value()) {
      ++snapshot.activeRuns;
    }

    if (run.announcePending) {
      ++snapshot.pendingAnnounce;
    }

    if (run.orphaned) {
      ++snapshot.orphanedRuns;
    }

    if (run.status == SubagentRunStatus::Timeout) {
      ++snapshot.timeoutRuns;
    }
  }

  std::sort(
      snapshot.runs.begin(),
      snapshot.runs.end(),
      [](const SubagentRunRecord& left, const SubagentRunRecord& right) {
        return left.runId < right.runId;
      });

  return snapshot;
}

bool SubagentRegistryService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig fixtureConfig;
  blazeclaw::config::AgentEntryConfig alpha;
  alpha.id = L"alpha";
  alpha.subagents.maxDepth = 1;
  alpha.subagents.allowAgents.push_back(L"alpha");
  alpha.subagents.allowAgents.push_back(L"beta");
  fixtureConfig.agents.entries.insert_or_assign(alpha.id, alpha);

  SubagentRegistryService fixtureService;
  fixtureService.Configure(fixtureConfig);
  fixtureService.Initialize(fixturesRoot / L"a3-registry");

  const auto allowedSpawn = fixtureService.Spawn(SubagentSpawnRequest{
      .requesterAgentId = "alpha",
      .targetAgentId = "beta",
      .parentSessionId = "main",
      .childSessionId = "child-1",
      .expectsCompletionMessage = true,
  });
  if (!allowedSpawn.accepted) {
    outError = L"Fixture validation failed: expected allowed subagent spawn.";
    return false;
  }

  const auto blockedSpawn = fixtureService.Spawn(SubagentSpawnRequest{
      .requesterAgentId = "alpha",
      .targetAgentId = "gamma",
      .parentSessionId = "main",
      .childSessionId = "child-2",
      .expectsCompletionMessage = true,
  });
  if (blockedSpawn.accepted) {
    outError = L"Fixture validation failed: expected blocked target agent.";
    return false;
  }

  const bool completed = fixtureService.CompleteRun(
      allowedSpawn.runId,
      SubagentRunStatus::Completed,
      "done",
      1735689601000);
  if (!completed) {
    outError = L"Fixture validation failed: expected completion update.";
    return false;
  }

  fixtureService.ProcessAnnounceQueue(1735689602000);
  fixtureService.ReconcileOrphans({"main"});

  const auto snapshot = fixtureService.Snapshot();
  if (snapshot.runs.empty()) {
    outError = L"Fixture validation failed: expected registry entries.";
    return false;
  }

  return true;
}

bool SubagentRegistryService::IsAllowedTarget(
    const std::string& requesterAgentId,
    const std::string& targetAgentId) const {
  if (requesterAgentId == targetAgentId) {
    return true;
  }

  const auto requesterIt = std::find_if(
      m_config.agents.entries.begin(),
      m_config.agents.entries.end(),
      [&requesterAgentId](const auto& pair) {
        return NormalizeAgentId(ToNarrow(pair.second.id.empty() ? pair.first : pair.second.id)) ==
            requesterAgentId;
      });

  if (requesterIt == m_config.agents.entries.end()) {
    return false;
  }

  const auto& allowAgents = requesterIt->second.subagents.allowAgents;
  if (allowAgents.empty()) {
    return false;
  }

  for (const auto& allowEntry : allowAgents) {
    const std::string allow = NormalizeAgentId(ToNarrow(allowEntry));
    if (allow == "*") {
      return true;
    }

    if (allow == targetAgentId) {
      return true;
    }
  }

  return false;
}

std::uint32_t SubagentRegistryService::ResolveMaxDepth(
    const std::string& requesterAgentId) const {
  const auto requesterIt = std::find_if(
      m_config.agents.entries.begin(),
      m_config.agents.entries.end(),
      [&requesterAgentId](const auto& pair) {
        return NormalizeAgentId(ToNarrow(pair.second.id.empty() ? pair.first : pair.second.id)) ==
            requesterAgentId;
      });

  if (requesterIt == m_config.agents.entries.end()) {
    return 3;
  }

  return requesterIt->second.subagents.maxDepth;
}

std::uint32_t SubagentRegistryService::ResolveChildDepth(
    const std::string& parentSessionId) const {
  std::uint32_t parentDepth = 0;
  for (const auto& [_, run] : m_runsById) {
    if (run.childSessionId != parentSessionId) {
      continue;
    }

    if (run.depth > parentDepth) {
      parentDepth = run.depth;
    }
  }

  return parentDepth + 1;
}

std::filesystem::path SubagentRegistryService::PersistencePath() const {
  return m_workspaceRoot / L".blazeclaw" / L"subagents.state";
}

void SubagentRegistryService::LoadState() {
  m_runsById.clear();

  const auto statePath = PersistencePath();
  std::ifstream input(statePath);
  if (!input.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }

    const auto parts = Split(line, '|');
    if (parts.size() < 13) {
      continue;
    }

    SubagentRunRecord run;
    run.runId = parts[0];
    run.parentSessionId = parts[1];
    run.childSessionId = parts[2];
    run.requesterAgentId = parts[3];
    run.targetAgentId = parts[4];
    run.depth = static_cast<std::uint32_t>(ParseUInt64(parts[5], 0));
    run.expectsCompletionMessage = parts[6] == "1";
    run.announcePending = parts[7] == "1";
    run.announceRetryCount = static_cast<std::uint32_t>(ParseUInt64(parts[8], 0));
    run.orphaned = parts[9] == "1";
    run.status = StatusFromString(parts[10]);
    run.startedAtMs = ParseUInt64(parts[11], 0);

    if (!parts[12].empty() && parts[12] != "-") {
      run.endedAtMs = ParseUInt64(parts[12], 0);
    }

    if (parts.size() >= 14) {
      run.summary = parts[13];
    }

    m_runsById.insert_or_assign(run.runId, run);
  }
}

void SubagentRegistryService::PersistState() const {
  const auto statePath = PersistencePath();
  std::error_code ec;
  std::filesystem::create_directories(statePath.parent_path(), ec);

  std::ofstream output(statePath, std::ios::out | std::ios::trunc);
  if (!output.is_open()) {
    return;
  }

  std::vector<std::string> runIds;
  runIds.reserve(m_runsById.size());
  for (const auto& [runId, _] : m_runsById) {
    runIds.push_back(runId);
  }

  std::sort(runIds.begin(), runIds.end());
  for (const auto& runId : runIds) {
    const auto it = m_runsById.find(runId);
    if (it == m_runsById.end()) {
      continue;
    }

    const auto& run = it->second;
    output << run.runId << "|"
           << run.parentSessionId << "|"
           << run.childSessionId << "|"
           << run.requesterAgentId << "|"
           << run.targetAgentId << "|"
           << run.depth << "|"
           << (run.expectsCompletionMessage ? "1" : "0") << "|"
           << (run.announcePending ? "1" : "0") << "|"
           << run.announceRetryCount << "|"
           << (run.orphaned ? "1" : "0") << "|"
           << StatusToString(run.status) << "|"
           << run.startedAtMs << "|"
           << (run.endedAtMs.has_value()
               ? std::to_string(run.endedAtMs.value())
               : std::string("-"))
           << "|" << run.summary << "\n";
  }
}

std::string SubagentRegistryService::NormalizeAgentId(
    const std::string& value) {
  const std::string trimmed = Trim(value);
  if (trimmed.empty()) {
    return "default";
  }

  std::string normalized;
  normalized.reserve(trimmed.size());
  for (const char ch : trimmed) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if ((lowered >= 'a' && lowered <= 'z') ||
        (lowered >= '0' && lowered <= '9') ||
        lowered == '-' ||
        lowered == '_') {
      normalized.push_back(lowered);
    }
  }

  if (normalized.empty()) {
    return "default";
  }

  return normalized;
}

std::string SubagentRegistryService::StatusToString(
    const SubagentRunStatus status) {
  switch (status) {
    case SubagentRunStatus::Running:
      return "running";
    case SubagentRunStatus::Completed:
      return "completed";
    case SubagentRunStatus::Failed:
      return "failed";
    case SubagentRunStatus::Timeout:
      return "timeout";
    default:
      return "running";
  }
}

SubagentRunStatus SubagentRegistryService::StatusFromString(
    const std::string& value) {
  if (value == "completed") {
    return SubagentRunStatus::Completed;
  }

  if (value == "failed") {
    return SubagentRunStatus::Failed;
  }

  if (value == "timeout") {
    return SubagentRunStatus::Timeout;
  }

  return SubagentRunStatus::Running;
}

} // namespace blazeclaw::core
