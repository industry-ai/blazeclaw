#include "pch.h"
#include "AgentsCatalogService.h"

#include <algorithm>
#include <cwctype>
#include <vector>

namespace blazeclaw::core {

namespace {

std::wstring Trim(const std::wstring& value) {
  const auto first = std::find_if_not(
      value.begin(),
      value.end(),
      [](const wchar_t ch) { return std::iswspace(ch) != 0; });
  const auto last = std::find_if_not(
      value.rbegin(),
      value.rend(),
      [](const wchar_t ch) { return std::iswspace(ch) != 0; })
                        .base();

  if (first >= last) {
    return {};
  }

  return std::wstring(first, last);
}

std::filesystem::path ResolvePath(
    const std::filesystem::path& workspaceRoot,
    const std::wstring& rawPath) {
  std::filesystem::path pathValue(rawPath);
  if (pathValue.is_relative()) {
    pathValue = workspaceRoot / pathValue;
  }

  std::error_code ec;
  const auto absolute = std::filesystem::absolute(pathValue, ec);
  if (ec) {
    return pathValue.lexically_normal();
  }

  return absolute.lexically_normal();
}

std::vector<std::wstring> Split(
    const std::wstring& value,
    const wchar_t delimiter) {
  std::vector<std::wstring> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto next = value.find(delimiter, start);
    if (next == std::wstring::npos) {
      parts.push_back(value.substr(start));
      break;
    }

    parts.push_back(value.substr(start, next - start));
    start = next + 1;
  }

  return parts;
}

} // namespace

std::wstring AgentsCatalogService::NormalizeAgentId(
    const std::wstring& rawValue) {
  std::wstring normalized;
  normalized.reserve(rawValue.size());

  for (const wchar_t ch : rawValue) {
    const wchar_t lowered = static_cast<wchar_t>(std::towlower(ch));
    if ((lowered >= L'a' && lowered <= L'z') ||
        (lowered >= L'0' && lowered <= L'9') ||
        lowered == L'-' ||
        lowered == L'_') {
      normalized.push_back(lowered);
      continue;
    }

    if (std::iswspace(lowered) != 0) {
      normalized.push_back(L'-');
    }
  }

  if (normalized.empty()) {
    return L"default";
  }

  if (!std::iswalnum(normalized.front())) {
    normalized.insert(normalized.begin(), L'a');
  }

  return normalized;
}

AgentScopeSnapshot AgentsCatalogService::BuildSnapshot(
    const std::filesystem::path& workspaceRoot,
    const blazeclaw::config::AppConfig& appConfig) const {
  AgentScopeSnapshot snapshot;
  const std::wstring configuredDefault =
      Trim(appConfig.agents.defaults.agentId);
  snapshot.defaultAgentId = NormalizeAgentId(
      configuredDefault.empty() ? L"default" : configuredDefault);

  std::vector<std::wstring> explicitDefaults;
  for (const auto& [key, rawEntry] : appConfig.agents.entries) {
    AgentScopeEntry entry;
    entry.id = NormalizeAgentId(
        rawEntry.id.empty() ? key : rawEntry.id);
    entry.name = rawEntry.name;
    entry.isDefault = rawEntry.isDefault;

    std::wstring selectedModel = Trim(rawEntry.model);
    if (selectedModel.empty()) {
      selectedModel = Trim(appConfig.agents.defaults.model);
    }

    if (selectedModel.empty()) {
      selectedModel = Trim(appConfig.agent.model);
    }

    entry.model = selectedModel;

    std::wstring workspaceConfig = Trim(rawEntry.workspace);
    if (workspaceConfig.empty()) {
      workspaceConfig = Trim(appConfig.agents.defaults.workspace);
    }

    if (!workspaceConfig.empty()) {
      entry.workspaceDir = ResolvePath(workspaceRoot, workspaceConfig);
    } else {
      std::wstring workspaceRootBase =
          Trim(appConfig.agents.defaults.workspaceRoot);
      if (!workspaceRootBase.empty()) {
        entry.workspaceDir =
            ResolvePath(workspaceRoot, workspaceRootBase) / entry.id;
      } else {
        entry.workspaceDir =
            ResolvePath(workspaceRoot, L".agents/workspaces") / entry.id;
      }
    }

    std::wstring agentDirConfig = Trim(rawEntry.agentDir);
    if (!agentDirConfig.empty()) {
      entry.agentDir = ResolvePath(workspaceRoot, agentDirConfig);
    } else {
      const std::wstring defaultAgentDirRoot =
          Trim(appConfig.agents.defaults.agentDirRoot);
      if (!defaultAgentDirRoot.empty()) {
        entry.agentDir =
            ResolvePath(workspaceRoot, defaultAgentDirRoot) / entry.id;
      } else {
        entry.agentDir = entry.workspaceDir / L".agents";
      }
    }

    if (entry.isDefault) {
      explicitDefaults.push_back(entry.id);
    }

    snapshot.entries.push_back(entry);
  }

  if (snapshot.entries.empty()) {
    AgentScopeEntry defaultEntry;
    defaultEntry.id = snapshot.defaultAgentId;
    defaultEntry.isDefault = true;
    defaultEntry.model = Trim(appConfig.agents.defaults.model);
    if (defaultEntry.model.empty()) {
      defaultEntry.model = Trim(appConfig.agent.model);
    }

    std::wstring workspaceBase = Trim(appConfig.agents.defaults.workspace);
    if (!workspaceBase.empty()) {
      defaultEntry.workspaceDir = ResolvePath(workspaceRoot, workspaceBase);
    } else {
      std::wstring workspaceRootBase =
          Trim(appConfig.agents.defaults.workspaceRoot);
      if (!workspaceRootBase.empty()) {
        defaultEntry.workspaceDir =
            ResolvePath(workspaceRoot, workspaceRootBase) / defaultEntry.id;
      } else {
        defaultEntry.workspaceDir =
            ResolvePath(workspaceRoot, L".agents/workspaces") / defaultEntry.id;
      }
    }

    std::wstring agentDirRoot = Trim(appConfig.agents.defaults.agentDirRoot);
    if (!agentDirRoot.empty()) {
      defaultEntry.agentDir =
          ResolvePath(workspaceRoot, agentDirRoot) / defaultEntry.id;
    } else {
      defaultEntry.agentDir = defaultEntry.workspaceDir / L".agents";
    }

    snapshot.entries.push_back(defaultEntry);
  }

  std::sort(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const AgentScopeEntry& left, const AgentScopeEntry& right) {
        return left.id < right.id;
      });

  if (explicitDefaults.size() > 1) {
    snapshot.diagnostics.duplicateDefaultAgents =
        static_cast<std::uint32_t>(explicitDefaults.size());
    snapshot.diagnostics.warnings.push_back(
        L"Multiple agents are marked default=true; using the first sorted id.");
  }

  if (!explicitDefaults.empty()) {
    snapshot.defaultAgentId = NormalizeAgentId(explicitDefaults.front());
  }

  const auto hasDefault = std::any_of(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [&snapshot](const AgentScopeEntry& entry) {
        return entry.id == snapshot.defaultAgentId;
      });
  if (!hasDefault && !snapshot.entries.empty()) {
    snapshot.defaultAgentId = snapshot.entries.front().id;
    snapshot.diagnostics.warnings.push_back(
        L"Configured default agent is missing; falling back to first sorted id.");
  }

  for (auto& entry : snapshot.entries) {
    entry.isDefault = (entry.id == snapshot.defaultAgentId);
  }

  return snapshot;
}

std::wstring AgentsCatalogService::ResolveAgentIdFromSessionKey(
    const std::wstring& sessionKey,
    const AgentScopeSnapshot& snapshot) const {
  const std::wstring trimmed = Trim(sessionKey);
  if (trimmed.empty()) {
    return snapshot.defaultAgentId;
  }

  const auto colonParts = Split(trimmed, L':');
  if (colonParts.size() >= 2) {
    if (NormalizeAgentId(colonParts[0]) == L"agent") {
      const auto fromAgentPrefix = NormalizeAgentId(colonParts[1]);
      if (!fromAgentPrefix.empty()) {
        return fromAgentPrefix;
      }
    }

    const auto direct = NormalizeAgentId(colonParts[0]);
    if (!direct.empty()) {
      return direct;
    }
  }

  return snapshot.defaultAgentId;
}

bool AgentsCatalogService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  const auto workspaceRoot = fixturesRoot / L"a1-scope" / L"workspace";
  blazeclaw::config::AppConfig config;
  config.agent.model = L"global-model";
  config.agents.defaults.agentId = L"alpha";
  config.agents.defaults.workspaceRoot = L"profiles";
  config.agents.defaults.agentDirRoot = L"agent-data";
  config.agents.defaults.model = L"default-model";

  blazeclaw::config::AgentEntryConfig alpha;
  alpha.id = L"alpha";
  alpha.name = L"Alpha";
  alpha.isDefault = true;
  alpha.model = L"alpha-model";
  config.agents.entries[alpha.id] = alpha;

  blazeclaw::config::AgentEntryConfig beta;
  beta.id = L"beta";
  beta.workspace = L"custom/beta";
  config.agents.entries[beta.id] = beta;

  const auto snapshot = BuildSnapshot(workspaceRoot, config);
  if (snapshot.entries.size() != 2) {
    outError = L"Fixture validation failed: expected two agent entries.";
    return false;
  }

  if (snapshot.defaultAgentId != L"alpha") {
    outError = L"Fixture validation failed: expected default agent alpha.";
    return false;
  }

  const auto betaEntryIt = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const AgentScopeEntry& entry) {
        return entry.id == L"beta";
      });
  if (betaEntryIt == snapshot.entries.end()) {
    outError = L"Fixture validation failed: beta entry missing.";
    return false;
  }

  const auto expectedBetaWorkspace =
      (workspaceRoot / L"custom" / L"beta").lexically_normal();
  if (betaEntryIt->workspaceDir.lexically_normal() != expectedBetaWorkspace) {
    outError =
        L"Fixture validation failed: beta workspace resolution mismatch.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
