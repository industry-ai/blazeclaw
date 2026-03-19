#include "pch.h"
#include "SkillsSyncService.h"

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <unordered_map>

namespace blazeclaw::core {

namespace {

std::wstring ToLower(const std::wstring& value) {
  std::wstring lowered = value;
  std::transform(
      lowered.begin(),
      lowered.end(),
      lowered.begin(),
      [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return lowered;
}

std::wstring SanitizePathSegment(const std::wstring& value) {
  std::wstring output;
  output.reserve(value.size());

  for (const wchar_t ch : value) {
    const bool safe =
        (ch >= L'a' && ch <= L'z') ||
        (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'0' && ch <= L'9') ||
        ch == L'-' || ch == L'_';
    output.push_back(safe ? ch : L'_');
  }

  if (output.empty()) {
    output = L"skill";
  }

  return output;
}

bool IsPathInside(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
  std::error_code ecA;
  std::error_code ecB;
  const auto canonicalRoot = std::filesystem::weakly_canonical(root, ecA);
  const auto canonicalCandidate =
      std::filesystem::weakly_canonical(candidate, ecB);
  if (ecA || ecB) {
    return false;
  }

  auto rootIt = canonicalRoot.begin();
  auto rootEnd = canonicalRoot.end();
  auto candidateIt = canonicalCandidate.begin();
  auto candidateEnd = canonicalCandidate.end();

  for (; rootIt != rootEnd; ++rootIt, ++candidateIt) {
    if (candidateIt == candidateEnd || *rootIt != *candidateIt) {
      return false;
    }
  }

  return true;
}

std::mutex& SyncMutex() {
  static std::mutex mutex;
  return mutex;
}

} // namespace

std::filesystem::path SkillsSyncService::ResolveSandboxRoot(
    const std::filesystem::path& workspaceRoot,
    std::vector<std::wstring>& outWarnings) {
  const auto defaultRoot = workspaceRoot / L".blazeclaw" / L"sandbox" / L"skills";

  wchar_t* envValue = nullptr;
  std::size_t envLength = 0;
  if (_wdupenv_s(&envValue, &envLength, L"BLAZECLAW_SKILLS_SANDBOX_DIR") != 0 ||
      envValue == nullptr ||
      envLength == 0) {
    if (envValue != nullptr) {
      free(envValue);
    }
    return defaultRoot;
  }

  const std::filesystem::path overridePath(envValue);
  free(envValue);

  std::filesystem::path resolved = overridePath;
  if (resolved.is_relative()) {
    resolved = workspaceRoot / resolved;
  }

  if (!IsPathInside(workspaceRoot, resolved)) {
    outWarnings.push_back(
        L"Sandbox sync override path is outside workspace root; using default sandbox path.");
    return defaultRoot;
  }

  return resolved;
}

SkillsSyncSnapshot SkillsSyncService::SyncToSandbox(
    const std::filesystem::path& workspaceRoot,
    const SkillsCatalogSnapshot& catalog,
    const SkillsEligibilitySnapshot& eligibility,
    const blazeclaw::config::AppConfig& appConfig) {
  std::lock_guard<std::mutex> lock(SyncMutex());

  SkillsSyncSnapshot snapshot;
  snapshot.sandboxSkillsRoot = ResolveSandboxRoot(workspaceRoot, snapshot.warnings);

  std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
  for (const auto& entry : eligibility.entries) {
    eligibilityByName.emplace(ToLower(entry.skillName), entry);
  }

  std::error_code ec;
  std::filesystem::remove_all(snapshot.sandboxSkillsRoot, ec);
  ec.clear();
  std::filesystem::create_directories(snapshot.sandboxSkillsRoot, ec);
  if (ec) {
    snapshot.warnings.push_back(L"Failed to create sandbox skills root directory.");
    snapshot.success = false;
    return snapshot;
  }

  for (const auto& entry : catalog.entries) {
    const auto eligibilityIt = eligibilityByName.find(ToLower(entry.skillName));
    if (eligibilityIt == eligibilityByName.end() || !eligibilityIt->second.eligible) {
      ++snapshot.skippedSkills;
      continue;
    }

    const auto targetDir = snapshot.sandboxSkillsRoot / SanitizePathSegment(entry.skillName);
    ec.clear();
    std::filesystem::create_directories(targetDir, ec);
    if (ec) {
      ++snapshot.skippedSkills;
      snapshot.warnings.push_back(
          L"Failed to create sandbox skill directory for " + entry.skillName + L".");
      continue;
    }

    ec.clear();
    std::filesystem::copy(
        entry.skillDir,
        targetDir,
        std::filesystem::copy_options::recursive |
            std::filesystem::copy_options::overwrite_existing,
        ec);
    if (ec) {
      ++snapshot.skippedSkills;
      snapshot.warnings.push_back(
          L"Failed to copy skill into sandbox: " + entry.skillName + L".");
      continue;
    }

    ++snapshot.copiedSkills;
  }

  snapshot.success = true;
  return snapshot;
}

bool SkillsSyncService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) {
  outError.clear();

  const auto workspace = fixturesRoot / L"s4-sync" / L"workspace";
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.limits.maxCandidatesPerRoot = 32;
  appConfig.skills.limits.maxSkillsLoadedPerSource = 32;

  const SkillsCatalogService catalogService;
  const SkillsEligibilityService eligibilityService;
  const auto catalog = catalogService.LoadCatalog(workspace, appConfig);
  const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);

  const auto snapshot = SyncToSandbox(workspace, catalog, eligibility, appConfig);
  if (!snapshot.success || snapshot.copiedSkills == 0) {
    outError = L"S4 sync fixture failed: expected at least one copied eligible skill.";
    return false;
  }

  const auto expectedSkillFile =
      snapshot.sandboxSkillsRoot / L"sync_skill_alpha" / L"SKILL.md";
  if (!std::filesystem::exists(expectedSkillFile)) {
    outError = L"S4 sync fixture failed: expected synced SKILL.md in sandbox root.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
