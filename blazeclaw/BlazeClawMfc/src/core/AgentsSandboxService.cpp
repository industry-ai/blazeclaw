#include "pch.h"
#include "AgentsSandboxService.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core {

std::string AgentsSandboxService::ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());
  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::string AgentsSandboxService::NormalizeId(const std::string& value) {
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

SandboxSnapshot AgentsSandboxService::BuildSnapshot(
    const AgentScopeSnapshot& scopeSnapshot,
    const blazeclaw::config::AppConfig& appConfig) const {
  SandboxSnapshot snapshot;
  snapshot.entries.reserve(scopeSnapshot.entries.size());

  const std::filesystem::path mirrorRoot =
      appConfig.sandbox.workspaceMirrorRoot.empty()
      ? std::filesystem::path(L".sandbox/workspaces")
      : std::filesystem::path(appConfig.sandbox.workspaceMirrorRoot);

  for (const auto& entry : scopeSnapshot.entries) {
    SandboxEntry runtime;
    runtime.agentId = NormalizeId(ToNarrow(entry.id));
    runtime.enabled = appConfig.sandbox.enabled;
    runtime.runtime = ToNarrow(appConfig.sandbox.runtime);
    runtime.workspaceMirror = mirrorRoot / std::filesystem::path(entry.id);
    runtime.browserEnabled = appConfig.sandbox.browserEnabled;
    runtime.allowHostNetwork = appConfig.sandbox.allowHostNetwork;

    if (runtime.enabled) {
      ++snapshot.enabledCount;
    }

    if (runtime.browserEnabled) {
      ++snapshot.browserEnabledCount;
    }

    snapshot.entries.push_back(std::move(runtime));
  }

  return snapshot;
}

SandboxPolicyDecision AgentsSandboxService::EvaluateToolPolicy(
    const SandboxSnapshot& snapshot,
    const std::string& agentId,
    const std::string& toolName) const {
  const auto normalizedAgent = NormalizeId(agentId);
  const auto it = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [&normalizedAgent](const SandboxEntry& entry) {
        return entry.agentId == normalizedAgent;
      });

  if (it == snapshot.entries.end()) {
    return SandboxPolicyDecision{
      .allowed = true,
      .reason = "agent_not_found",
    };
  }

  if (!it->enabled) {
    return SandboxPolicyDecision{
      .allowed = true,
      .reason = "sandbox_disabled",
    };
  }

  const std::string loweredTool = NormalizeId(toolName);
  if (!it->allowHostNetwork &&
      (loweredTool == "web_fetch" || loweredTool == "network")) {
    return SandboxPolicyDecision{
      .allowed = false,
      .reason = "network_disallowed_by_sandbox_policy",
    };
  }

  if (!it->browserEnabled && loweredTool == "browser") {
    return SandboxPolicyDecision{
      .allowed = false,
      .reason = "browser_disabled_by_sandbox_policy",
    };
  }

  return SandboxPolicyDecision{
    .allowed = true,
    .reason = "allowed",
  };
}

bool AgentsSandboxService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  AgentScopeSnapshot scope;
  scope.defaultAgentId = L"alpha";
  scope.entries.push_back(AgentScopeEntry{ .id = L"alpha" });

  blazeclaw::config::AppConfig config;
  config.sandbox.enabled = true;
  config.sandbox.runtime = L"docker";
  config.sandbox.allowHostNetwork = false;
  config.sandbox.browserEnabled = false;
  config.sandbox.workspaceMirrorRoot = L"mirror";

  const auto snapshot = BuildSnapshot(scope, config);
  if (snapshot.entries.empty() || snapshot.enabledCount != 1) {
    outError = L"Fixture validation failed: expected enabled sandbox entry.";
    return false;
  }

  const auto browserDecision = EvaluateToolPolicy(snapshot, "alpha", "browser");
  if (browserDecision.allowed) {
    outError = L"Fixture validation failed: browser should be blocked.";
    return false;
  }

  const auto writeDecision = EvaluateToolPolicy(snapshot, "alpha", "write");
  if (!writeDecision.allowed) {
    outError = L"Fixture validation failed: write tool should be allowed.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
