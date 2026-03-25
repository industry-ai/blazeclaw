#include "pch.h"
#include "SkillsPromptService.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <sstream>
#include <unordered_map>

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

std::wstring CompactHomePath(const std::filesystem::path& pathValue) {
  std::filesystem::path home;
  wchar_t* homeValue = nullptr;
  std::size_t length = 0;
  if (_wdupenv_s(&homeValue, &length, L"USERPROFILE") == 0 &&
      homeValue != nullptr &&
      length > 0) {
    home = std::filesystem::path(homeValue);
  }
  if (homeValue != nullptr) {
    free(homeValue);
  }

  const auto pathText = pathValue.wstring();
  if (!home.empty()) {
    const auto homeText = home.wstring();
    if (!homeText.empty() &&
        pathText.size() > homeText.size() &&
        pathText.rfind(homeText, 0) == 0) {
      return L"~" + pathText.substr(homeText.size());
    }
  }

  return pathText;
}

bool IncludesSkill(
    const std::vector<std::wstring>& includedSkills,
    const std::wstring& skillName) {
  const auto target = ToLower(Trim(skillName));
  return std::any_of(
      includedSkills.begin(),
      includedSkills.end(),
      [&](const std::wstring& item) {
        return ToLower(Trim(item)) == target;
      });
}

bool IsSelfEvolvingHookBridgeReady(const SkillsCatalogSnapshot& catalog) {
  const std::wstring target = L"self-evolving";
  for (const auto& entry : catalog.entries) {
    if (ToLower(Trim(entry.skillName)) != target) {
      continue;
    }

    std::error_code ec;
    const auto hookDoc = entry.skillDir / L"hooks" / L"blazeclaw" / L"HOOK.md";
    const auto hookHandler =
        entry.skillDir / L"hooks" / L"blazeclaw" / L"handler.ts";
    const bool hasHookDoc = std::filesystem::is_regular_file(hookDoc, ec) && !ec;
    ec.clear();
    const bool hasHookHandler =
        std::filesystem::is_regular_file(hookHandler, ec) && !ec;
    return hasHookDoc && hasHookHandler;
  }

  return false;
}

std::wstring BuildSelfEvolvingReminderBlock() {
  std::wstringstream reminder;
  reminder << L"\n## Self-Evolving Reminder\n";
  reminder << L"When tasks finish, capture reusable learnings:\n";
  reminder << L"- corrections -> .learnings/LEARNINGS.md\n";
  reminder << L"- failures -> .learnings/ERRORS.md\n";
  reminder << L"- missing capabilities -> .learnings/FEATURE_REQUESTS.md\n";
  reminder << L"Promote proven patterns to AGENTS.md / SOUL.md / TOOLS.md.\n";
  return reminder.str();
}

} // namespace

std::optional<std::vector<std::wstring>> SkillsPromptService::NormalizeFilter(
    const std::optional<std::vector<std::wstring>>& input) {
  if (!input.has_value()) {
    return std::nullopt;
  }

  std::vector<std::wstring> normalized;
  for (const auto& item : input.value()) {
    const auto lowered = ToLower(Trim(item));
    if (!lowered.empty()) {
      normalized.push_back(lowered);
    }
  }

  std::sort(normalized.begin(), normalized.end());
  normalized.erase(
      std::unique(normalized.begin(), normalized.end()),
      normalized.end());
  return normalized;
}

SkillsPromptSnapshot SkillsPromptService::BuildSnapshot(
    const SkillsCatalogSnapshot& catalog,
    const SkillsEligibilitySnapshot& eligibility,
    const blazeclaw::config::AppConfig& appConfig,
    const std::optional<std::vector<std::wstring>>& skillFilter) const {
  SkillsPromptSnapshot snapshot;
  snapshot.totalEligible = eligibility.eligibleCount;
  snapshot.filter = NormalizeFilter(skillFilter);

  std::unordered_map<std::wstring, bool> eligibilityMap;
  std::unordered_map<std::wstring, bool> modelInvocationMap;
  for (const auto& item : eligibility.entries) {
    const auto key = ToLower(Trim(item.skillName));
    eligibilityMap[key] = item.eligible;
    modelInvocationMap[key] = item.disableModelInvocation;
  }

  const auto includeFilter = snapshot.filter.has_value() &&
      !snapshot.filter->empty();

  std::wstringstream builder;
  builder << L"# Skills\n";
  builder << L"Available skill instructions for this run:\n";

  std::uint32_t included = 0;
  for (const auto& entry : catalog.entries) {
    const auto key = ToLower(Trim(entry.skillName));
    const auto eligibilityIt = eligibilityMap.find(key);
    if (eligibilityIt == eligibilityMap.end() || !eligibilityIt->second) {
      continue;
    }

    const auto modelInvocationIt = modelInvocationMap.find(key);
    if (modelInvocationIt != modelInvocationMap.end() &&
        modelInvocationIt->second) {
      continue;
    }

    if (includeFilter &&
        std::find(snapshot.filter->begin(), snapshot.filter->end(), key) ==
            snapshot.filter->end()) {
      continue;
    }

    if (included >= appConfig.skills.limits.maxSkillsInPrompt) {
      snapshot.truncated = true;
      break;
    }

    builder << L"- " << entry.skillName << L": " << entry.description;
    builder << L" (" << CompactHomePath(entry.skillFile) << L")\n";
    snapshot.includedSkills.push_back(entry.skillName);
    ++included;
  }

  if (IncludesSkill(snapshot.includedSkills, L"self-evolving") &&
      IsSelfEvolvingHookBridgeReady(catalog)) {
    builder << BuildSelfEvolvingReminderBlock();
  }

  snapshot.prompt = builder.str();
  if (snapshot.prompt.size() > appConfig.skills.limits.maxSkillsPromptChars) {
    snapshot.prompt = snapshot.prompt.substr(
        0,
        appConfig.skills.limits.maxSkillsPromptChars);
    snapshot.truncated = true;
  }

  snapshot.includedCount = static_cast<std::uint32_t>(snapshot.includedSkills.size());
  snapshot.promptChars = static_cast<std::uint32_t>(snapshot.prompt.size());
  return snapshot;
}

bool SkillsPromptService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  const auto root = fixturesRoot / L"s2-prompt" / L"workspace";
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.limits.maxSkillsInPrompt = 1;
  appConfig.skills.limits.maxSkillsPromptChars = 150;

  const SkillsCatalogService catalogService;
  const SkillsEligibilityService eligibilityService;

  const auto catalog = catalogService.LoadCatalog(root, appConfig);
  const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
  const auto snapshot = BuildSnapshot(catalog, eligibility, appConfig);
  if (!snapshot.truncated) {
    outError = L"S2 prompt fixture failed: expected prompt truncation.";
    return false;
  }

  if (snapshot.prompt.find(L"## Self-Evolving Reminder") != std::wstring::npos) {
    outError = L"S2 prompt fixture failed: self-evolving reminder should not appear without self-evolving skill.";
    return false;
  }

  if (snapshot.includedCount != 1) {
    outError = L"S2 prompt fixture failed: expected maxSkillsInPrompt limit of 1.";
    return false;
  }

  const std::optional<std::vector<std::wstring>> filter =
      std::vector<std::wstring>{L"prompt-skill-b"};
  const auto filtered = BuildSnapshot(catalog, eligibility, appConfig, filter);
  if (filtered.includedSkills.empty() ||
      ToLower(filtered.includedSkills.front()) != L"prompt-skill-b") {
    outError = L"S2 prompt fixture failed: expected filter to include prompt-skill-b.";
    return false;
  }

  const auto selfEvolvingRoot =
      fixturesRoot / L"s7-self-evolving" / L"workspace";
  blazeclaw::config::AppConfig selfEvolvingConfig;
  selfEvolvingConfig.skills.limits.maxSkillsInPrompt = 8;
  selfEvolvingConfig.skills.limits.maxSkillsPromptChars = 4000;
  selfEvolvingConfig.skills.limits.maxCandidatesPerRoot = 32;
  selfEvolvingConfig.skills.limits.maxSkillsLoadedPerSource = 32;
  selfEvolvingConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

  const auto selfCatalog = catalogService.LoadCatalog(selfEvolvingRoot, selfEvolvingConfig);
  const auto selfEligibility = eligibilityService.Evaluate(selfCatalog, selfEvolvingConfig);
  const auto selfSnapshot = BuildSnapshot(selfCatalog, selfEligibility, selfEvolvingConfig);
  if (selfSnapshot.prompt.find(L"## Self-Evolving Reminder") == std::wstring::npos) {
    outError = L"S7 self-evolving fixture failed: expected self-evolving reminder injection in prompt.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
