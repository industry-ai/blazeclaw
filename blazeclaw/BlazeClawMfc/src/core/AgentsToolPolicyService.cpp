#include "pch.h"
#include "AgentsToolPolicyService.h"

#include <algorithm>
#include <cctype>

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

std::string NormalizeToolName(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if ((lowered >= 'a' && lowered <= 'z') ||
        (lowered >= '0' && lowered <= '9') ||
        lowered == '_' || lowered == '-' || lowered == '.') {
      normalized.push_back(lowered);
    }
  }

  return normalized;
}

void AddUnique(
    std::vector<std::string>& target,
    const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(target.begin(), target.end(), value) != target.end()) {
    return;
  }

  target.push_back(value);
}

} // namespace

std::vector<std::string> AgentsToolPolicyService::NormalizeToolList(
    const std::vector<std::wstring>& values) {
  std::vector<std::string> normalized;
  normalized.reserve(values.size());

  for (const auto& value : values) {
    const std::string tool = NormalizeToolName(ToNarrow(value));
    if (!tool.empty()) {
      AddUnique(normalized, tool);
    }
  }

  return normalized;
}

std::string AgentsToolPolicyService::NormalizeAgentId(const std::string& value) {
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

std::vector<std::string> AgentsToolPolicyService::CoreToolsForProfile(
    const std::string& profile,
    const std::vector<std::string>& catalog) {
  if (profile == "full") {
    return catalog;
  }

  const std::set<std::string> minimal = {
      "session_status",
      "sessions_list",
      "agents_list",
  };

  const std::set<std::string> messaging = {
      "session_status",
      "sessions_list",
      "sessions_send",
      "message",
  };

  const std::set<std::string> coding = {
      "read",
      "write",
      "edit",
      "apply_patch",
      "exec",
      "process",
      "session_status",
      "sessions_list",
      "sessions_history",
      "sessions_send",
      "sessions_spawn",
      "subagents",
      "agents_list",
  };

  const std::set<std::string>* selected = &minimal;
  if (profile == "coding") {
    selected = &coding;
  } else if (profile == "messaging") {
    selected = &messaging;
  }

  std::vector<std::string> resolved;
  for (const auto& tool : catalog) {
    if (selected->find(tool) != selected->end()) {
      AddUnique(resolved, tool);
    }
  }

  return resolved;
}

AgentsToolPolicySnapshot AgentsToolPolicyService::BuildSnapshot(
    const AgentScopeSnapshot& scopeSnapshot,
    const blazeclaw::config::AppConfig& appConfig) const {
  AgentsToolPolicySnapshot snapshot;
  snapshot.coreCatalog = {
      "read",
      "write",
      "edit",
      "apply_patch",
      "exec",
      "process",
      "web_search",
      "web_fetch",
      "memory_search",
      "memory_get",
      "sessions_list",
      "sessions_history",
      "sessions_send",
      "sessions_spawn",
      "subagents",
      "session_status",
      "browser",
      "canvas",
      "message",
      "cron",
      "gateway",
      "nodes",
      "agents_list",
      "image",
      "tts",
  };

  for (const auto& scopeEntry : scopeSnapshot.entries) {
    AgentToolPolicyEntry policy;
    policy.agentId = NormalizeAgentId(ToNarrow(scopeEntry.id));
    policy.profile = "full";

    const auto configIt = std::find_if(
        appConfig.agents.entries.begin(),
        appConfig.agents.entries.end(),
        [&policy](const auto& pair) {
          const std::wstring configuredId =
              pair.second.id.empty() ? pair.first : pair.second.id;
          return NormalizeAgentId(ToNarrow(configuredId)) == policy.agentId;
        });

    if (configIt != appConfig.agents.entries.end()) {
      const auto configuredProfile = NormalizeToolName(ToNarrow(configIt->second.tools.profile));
      if (!configuredProfile.empty()) {
        policy.profile = configuredProfile;
      }

      policy.allowedTools = NormalizeToolList(configIt->second.tools.allow);
      policy.deniedTools = NormalizeToolList(configIt->second.tools.deny);
      policy.ownerOnlyTools = NormalizeToolList(configIt->second.tools.ownerOnly);
    }

    policy.resolvedTools = CoreToolsForProfile(policy.profile, snapshot.coreCatalog);

    for (const auto& tool : policy.allowedTools) {
      if (tool == "*") {
        policy.resolvedTools = snapshot.coreCatalog;
        continue;
      }

      AddUnique(policy.resolvedTools, tool);
    }

    if (!policy.deniedTools.empty()) {
      policy.resolvedTools.erase(
          std::remove_if(
              policy.resolvedTools.begin(),
              policy.resolvedTools.end(),
              [&policy](const std::string& tool) {
                return std::find(
                    policy.deniedTools.begin(),
                    policy.deniedTools.end(),
                    tool) != policy.deniedTools.end();
              }),
          policy.resolvedTools.end());
    }

    snapshot.entries.push_back(std::move(policy));
  }

  return snapshot;
}

std::vector<std::string> AgentsToolPolicyService::ResolveToolsForAgent(
    const AgentsToolPolicySnapshot& snapshot,
    const std::string& agentId,
    const bool senderIsOwner) const {
  const std::string normalizedAgentId = NormalizeAgentId(agentId);
  const auto it = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [&normalizedAgentId](const AgentToolPolicyEntry& entry) {
        return entry.agentId == normalizedAgentId;
      });

  if (it == snapshot.entries.end()) {
    return snapshot.coreCatalog;
  }

  if (senderIsOwner || it->ownerOnlyTools.empty()) {
    return it->resolvedTools;
  }

  std::vector<std::string> filtered;
  filtered.reserve(it->resolvedTools.size());
  for (const auto& tool : it->resolvedTools) {
    if (std::find(
            it->ownerOnlyTools.begin(),
            it->ownerOnlyTools.end(),
            tool) != it->ownerOnlyTools.end()) {
      continue;
    }

    filtered.push_back(tool);
  }

  return filtered;
}

bool AgentsToolPolicyService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  AgentScopeSnapshot scope;
  scope.defaultAgentId = L"alpha";
  scope.entries.push_back(AgentScopeEntry{ .id = L"alpha" });

  blazeclaw::config::AppConfig config;
  blazeclaw::config::AgentEntryConfig alpha;
  alpha.id = L"alpha";
  alpha.tools.profile = L"minimal";
  alpha.tools.allow.push_back(L"exec");
  alpha.tools.deny.push_back(L"sessions_list");
  alpha.tools.ownerOnly.push_back(L"exec");
  config.agents.entries.insert_or_assign(alpha.id, alpha);

  const auto snapshot = BuildSnapshot(scope, config);
  if (snapshot.entries.empty()) {
    outError = L"Fixture validation failed: expected tool policy entry.";
    return false;
  }

  const auto ownerTools = ResolveToolsForAgent(snapshot, "alpha", true);
  if (std::find(ownerTools.begin(), ownerTools.end(), "exec") == ownerTools.end()) {
    outError = L"Fixture validation failed: owner should retain owner-only tool.";
    return false;
  }

  const auto nonOwnerTools = ResolveToolsForAgent(snapshot, "alpha", false);
  if (std::find(nonOwnerTools.begin(), nonOwnerTools.end(), "exec") != nonOwnerTools.end()) {
    outError = L"Fixture validation failed: non-owner should not have owner-only tool.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
