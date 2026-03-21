#include "pch.h"
#include "PiEmbeddedService.h"

namespace blazeclaw::core {

void PiEmbeddedService::Configure(const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
}

EmbeddedRunResult PiEmbeddedService::QueueRun(const EmbeddedRunRequest& request) {
  if (!m_config.embedded.enabled) {
    return EmbeddedRunResult{
      .accepted = false,
      .runId = {},
      .status = "blocked",
      .reason = "embedded_runtime_disabled",
      .startedAtMs = 0,
    };
  }

  if (ActiveRuns() >= m_config.embedded.maxQueueDepth) {
    return EmbeddedRunResult{
      .accepted = false,
      .runId = {},
      .status = "blocked",
      .reason = "embedded_queue_full",
      .startedAtMs = 0,
    };
  }

  const std::uint64_t startedAtMs = 1735689700000 + static_cast<std::uint64_t>(m_runsById.size());
  const std::string runId =
      "embedded-" + std::to_string(startedAtMs) + "-" +
      (request.agentId.empty() ? "default" : request.agentId);

  EmbeddedRunRecord record;
  record.runId = runId;
  record.sessionId = request.sessionId.empty() ? "main" : request.sessionId;
  record.agentId = request.agentId.empty() ? "default" : request.agentId;
  record.message = request.message;
  record.status = "running";
  record.startedAtMs = startedAtMs;

  m_runsById.insert_or_assign(runId, record);

  return EmbeddedRunResult{
    .accepted = true,
    .runId = runId,
    .status = "running",
    .reason = "accepted",
    .startedAtMs = startedAtMs,
  };
}

bool PiEmbeddedService::CompleteRun(
    const std::string& runId,
    const std::string& status,
    const std::uint64_t completedAtMs) {
  const auto it = m_runsById.find(runId);
  if (it == m_runsById.end()) {
    return false;
  }

  it->second.status = status.empty() ? "completed" : status;
  it->second.completedAtMs = completedAtMs;
  return true;
}

std::size_t PiEmbeddedService::ActiveRuns() const {
  std::size_t active = 0;
  for (const auto& [_, run] : m_runsById) {
    if (!run.completedAtMs.has_value()) {
      ++active;
    }
  }

  return active;
}

std::optional<EmbeddedRunRecord> PiEmbeddedService::GetRun(
    const std::string& runId) const {
  const auto it = m_runsById.find(runId);
  if (it == m_runsById.end()) {
    return std::nullopt;
  }

  return it->second;
}

bool PiEmbeddedService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig cfg;
  cfg.embedded.enabled = true;
  cfg.embedded.maxQueueDepth = 1;

  PiEmbeddedService service;
  service.Configure(cfg);

  const auto first = service.QueueRun(EmbeddedRunRequest{
      .sessionId = "main",
      .agentId = "alpha",
      .message = "hello",
  });

  if (!first.accepted) {
    outError = L"Fixture validation failed: expected first embedded run accepted.";
    return false;
  }

  const auto second = service.QueueRun(EmbeddedRunRequest{
      .sessionId = "main",
      .agentId = "beta",
      .message = "hello2",
  });

  if (second.accepted || second.reason != "embedded_queue_full") {
    outError = L"Fixture validation failed: expected queue-full rejection.";
    return false;
  }

  if (!service.CompleteRun(first.runId, "completed", first.startedAtMs + 1)) {
    outError = L"Fixture validation failed: expected run completion success.";
    return false;
  }

  const auto resolved = service.GetRun(first.runId);
  if (!resolved.has_value() || resolved->status != "completed") {
    outError = L"Fixture validation failed: expected completed embedded run status.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
