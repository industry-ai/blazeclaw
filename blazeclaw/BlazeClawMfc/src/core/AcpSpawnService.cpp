#include "pch.h"
#include "AcpSpawnService.h"

namespace blazeclaw::core {

namespace {

std::string ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());

  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::string NormalizeAgentId(const std::string& raw) {
  if (raw.empty()) {
    return "default";
  }

  std::string normalized;
  normalized.reserve(raw.size());
  for (const char ch : raw) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if ((lowered >= 'a' && lowered <= 'z') ||
        (lowered >= '0' && lowered <= '9') ||
        lowered == '-' ||
        lowered == '_') {
      normalized.push_back(lowered);
    }
  }

  return normalized.empty() ? "default" : normalized;
}

} // namespace

AcpSpawnDecision AcpSpawnService::Evaluate(
    const blazeclaw::config::AppConfig& appConfig,
    const AcpSpawnRequest& request) const {
  if (!appConfig.acp.enabled) {
    return AcpSpawnDecision{
      .allowed = false,
      .resolvedAgentId = {},
      .mode = "blocked",
      .reason = "acp_disabled",
    };
  }

  if (request.requesterSandboxed) {
    return AcpSpawnDecision{
      .allowed = false,
      .resolvedAgentId = {},
      .mode = "blocked",
      .reason = "sandbox_session_cannot_spawn_acp",
    };
  }

  if (request.threadRequested && !appConfig.acp.allowThreadSpawn) {
    return AcpSpawnDecision{
      .allowed = false,
      .resolvedAgentId = {},
      .mode = "blocked",
      .reason = "thread_spawn_disabled",
    };
  }

  const std::string requestedAgent = NormalizeAgentId(request.targetAgentId);
  const std::string defaultAgent = NormalizeAgentId(ToNarrow(appConfig.acp.defaultAgent));
  const std::string resolvedAgentId =
      requestedAgent.empty() || requestedAgent == "default"
      ? defaultAgent
      : requestedAgent;

  return AcpSpawnDecision{
    .allowed = true,
    .resolvedAgentId = resolvedAgentId.empty() ? "default" : resolvedAgentId,
    .mode = request.threadRequested ? "session" : "run",
    .reason = "accepted",
  };
}

bool AcpSpawnService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;
  config.acp.enabled = true;
  config.acp.defaultAgent = L"alpha";
  config.acp.allowThreadSpawn = false;

  const auto blockedThread = Evaluate(config, AcpSpawnRequest{
      .requesterSessionId = "main",
      .requesterAgentId = "alpha",
      .targetAgentId = "beta",
      .threadRequested = true,
      .requesterSandboxed = false,
  });

  if (blockedThread.allowed || blockedThread.reason != "thread_spawn_disabled") {
    outError = L"Fixture validation failed: expected ACP thread spawn to be blocked.";
    return false;
  }

  config.acp.allowThreadSpawn = true;
  const auto accepted = Evaluate(config, AcpSpawnRequest{
      .requesterSessionId = "main",
      .requesterAgentId = "alpha",
      .targetAgentId = "",
      .threadRequested = false,
      .requesterSandboxed = false,
  });

  if (!accepted.allowed || accepted.resolvedAgentId != "alpha") {
    outError = L"Fixture validation failed: expected ACP spawn to resolve default agent.";
    return false;
  }

  const auto sandboxBlocked = Evaluate(config, AcpSpawnRequest{
      .requesterSessionId = "main",
      .requesterAgentId = "alpha",
      .targetAgentId = "beta",
      .threadRequested = false,
      .requesterSandboxed = true,
  });

  if (sandboxBlocked.allowed || sandboxBlocked.reason != "sandbox_session_cannot_spawn_acp") {
    outError = L"Fixture validation failed: expected sandbox ACP spawn to be blocked.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
