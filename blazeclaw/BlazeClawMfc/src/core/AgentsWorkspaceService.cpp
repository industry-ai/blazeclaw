#include "pch.h"
#include "AgentsWorkspaceService.h"

#include <fstream>

namespace blazeclaw::core {

namespace {

constexpr std::wstring_view kWorkspaceStatePath =
    L".openclaw/workspace-state.json";

const std::vector<std::wstring> kRequiredBootstrapFiles = {
    L"AGENTS.md",
    L"SOUL.md",
    L"TOOLS.md",
    L"IDENTITY.md",
    L"USER.md",
    L"HEARTBEAT.md",
    L"BOOTSTRAP.md",
    L"MEMORY.md",
};

std::wstring ReadUtf8AsWide(const std::filesystem::path& filePath) {
  std::ifstream input(filePath, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }

  const std::string bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  if (bytes.empty()) {
    return {};
  }

  const int required = MultiByteToWideChar(
      CP_UTF8,
      0,
      bytes.c_str(),
      static_cast<int>(bytes.size()),
      nullptr,
      0);
  if (required <= 0) {
    return std::wstring(bytes.begin(), bytes.end());
  }

  std::wstring output(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(
      CP_UTF8,
      0,
      bytes.c_str(),
      static_cast<int>(bytes.size()),
      output.data(),
      required);
  if (converted <= 0) {
    return std::wstring(bytes.begin(), bytes.end());
  }

  return output;
}

} // namespace

bool AgentsWorkspaceService::IsOnboardingCompleted(
    const std::filesystem::path& workspaceDir) {
  const auto statePath = workspaceDir / kWorkspaceStatePath;
  const std::wstring payload = ReadUtf8AsWide(statePath);
  if (payload.empty()) {
    return false;
  }

  return payload.find(L"\"onboardingCompletedAt\"") != std::wstring::npos;
}

AgentsWorkspaceSnapshot AgentsWorkspaceService::BuildSnapshot(
    const AgentScopeSnapshot& scopeSnapshot) const {
  AgentsWorkspaceSnapshot snapshot;
  snapshot.entries.reserve(scopeSnapshot.entries.size());

  for (const auto& scopeEntry : scopeSnapshot.entries) {
    AgentWorkspaceEntry workspaceEntry;
    workspaceEntry.agentId = scopeEntry.id;
    workspaceEntry.workspaceDir = scopeEntry.workspaceDir;
    workspaceEntry.onboardingCompleted =
        IsOnboardingCompleted(scopeEntry.workspaceDir);

    for (const auto& bootstrapFile : kRequiredBootstrapFiles) {
      std::error_code ec;
      const auto filePath = scopeEntry.workspaceDir / bootstrapFile;
      if (!std::filesystem::is_regular_file(filePath, ec) || ec) {
        workspaceEntry.missingBootstrapFiles.push_back(bootstrapFile);
      }
    }

    if (workspaceEntry.onboardingCompleted) {
      ++snapshot.onboardingCompletedCount;
    }

    if (!workspaceEntry.onboardingCompleted &&
        workspaceEntry.missingBootstrapFiles.empty()) {
      snapshot.warnings.push_back(
          L"Agent workspace has bootstrap files but onboarding is incomplete: " +
          workspaceEntry.agentId);
    }

    snapshot.entries.push_back(std::move(workspaceEntry));
  }

  return snapshot;
}

bool AgentsWorkspaceService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  AgentScopeSnapshot scope;
  scope.defaultAgentId = L"alpha";

  AgentScopeEntry alpha;
  alpha.id = L"alpha";
  alpha.workspaceDir = fixturesRoot / L"a1-workspace" / L"alpha";
  scope.entries.push_back(alpha);

  AgentScopeEntry beta;
  beta.id = L"beta";
  beta.workspaceDir = fixturesRoot / L"a1-workspace" / L"beta";
  scope.entries.push_back(beta);

  const auto snapshot = BuildSnapshot(scope);
  if (snapshot.entries.size() != 2) {
    outError = L"Fixture validation failed: expected two workspace entries.";
    return false;
  }

  const auto alphaIt = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const AgentWorkspaceEntry& entry) {
        return entry.agentId == L"alpha";
      });
  if (alphaIt == snapshot.entries.end() || !alphaIt->onboardingCompleted) {
    outError =
        L"Fixture validation failed: alpha should be onboarding-complete.";
    return false;
  }

  const auto betaIt = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const AgentWorkspaceEntry& entry) {
        return entry.agentId == L"beta";
      });
  if (betaIt == snapshot.entries.end()) {
    outError = L"Fixture validation failed: beta workspace entry missing.";
    return false;
  }

  if (betaIt->missingBootstrapFiles.empty()) {
    outError =
        L"Fixture validation failed: beta should report missing bootstrap files.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
