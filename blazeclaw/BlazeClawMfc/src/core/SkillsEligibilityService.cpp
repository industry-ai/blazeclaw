#include "pch.h"
#include "SkillsEligibilityService.h"

#include <algorithm>
#include <cwctype>

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

bool ParseBoolField(const std::wstring& value, const bool fallback) {
  const std::wstring normalized = ToLower(Trim(value));
  if (normalized == L"true" || normalized == L"1" || normalized == L"yes") {
    return true;
  }

  if (normalized == L"false" || normalized == L"0" || normalized == L"no") {
    return false;
  }

  return fallback;
}

std::vector<std::wstring> SplitList(const std::wstring& raw) {
  std::vector<std::wstring> values;
  std::wstring current;
  current.reserve(raw.size());

  const auto flush = [&values, &current]() {
    const std::wstring trimmed = Trim(current);
    if (!trimmed.empty()) {
      values.push_back(ToLower(trimmed));
    }
    current.clear();
  };

  for (const wchar_t ch : raw) {
    if (ch == L',' || ch == L';' || ch == L'|') {
      flush();
      continue;
    }

    current.push_back(ch);
  }

  flush();
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

std::wstring GetFrontmatterField(
    const SkillFrontmatter& frontmatter,
    std::initializer_list<const wchar_t*> keys) {
  for (const auto* key : keys) {
    const auto it = frontmatter.fields.find(ToLower(key));
    if (it != frontmatter.fields.end()) {
      return it->second;
    }
  }

  return {};
}

bool HasBinary(const std::wstring& binName) {
  const std::wstring name = Trim(binName);
  if (name.empty()) {
    return false;
  }

  const DWORD required = SearchPathW(
      nullptr,
      name.c_str(),
      nullptr,
      0,
      nullptr,
      nullptr);
  return required > 0;
}

bool HasEnvironmentValue(const std::wstring& envName) {
  wchar_t* value = nullptr;
  std::size_t valueLength = 0;
  const errno_t status = _wdupenv_s(
      &value,
      &valueLength,
      envName.c_str());
  if (status != 0 || value == nullptr || valueLength == 0) {
    if (value != nullptr) {
      free(value);
    }
    return false;
  }

  const bool hasValue = value[0] != L'\0';
  free(value);
  return hasValue;
}

bool IsCurrentPlatformAllowed(const std::vector<std::wstring>& osList) {
  if (osList.empty()) {
    return true;
  }

  constexpr wchar_t kPlatformA[] = L"win32";
  constexpr wchar_t kPlatformB[] = L"windows";
  return std::find(osList.begin(), osList.end(), kPlatformA) != osList.end() ||
         std::find(osList.begin(), osList.end(), kPlatformB) != osList.end();
}

bool IsConfigPathTruthy(
    const blazeclaw::config::AppConfig& appConfig,
    const std::wstring& configPath) {
  const std::wstring path = ToLower(Trim(configPath));
  if (path == L"gateway.bind" || path == L"gateway.bindaddress") {
    return !appConfig.gateway.bindAddress.empty();
  }

  if (path == L"gateway.port") {
    return appConfig.gateway.port > 0;
  }

  if (path == L"agent.model") {
    return !appConfig.agent.model.empty();
  }

  if (path == L"agent.streaming") {
    return appConfig.agent.enableStreaming;
  }

  if (path == L"skills.load.watch") {
    return appConfig.skills.load.watch;
  }

  return false;
}

bool IsBundledAllowed(
    const SkillsCatalogEntry& catalogEntry,
    const SkillsEligibilityEntry& eligibility,
    const blazeclaw::config::AppConfig& appConfig) {
  if (catalogEntry.sourceKind != SkillsSourceKind::Bundled) {
    return true;
  }

  if (appConfig.skills.allowBundled.empty()) {
    return true;
  }

  const std::wstring normalizedKey = ToLower(eligibility.skillKey);
  const std::wstring normalizedName = ToLower(catalogEntry.skillName);
  for (const auto& item : appConfig.skills.allowBundled) {
    const std::wstring normalized = ToLower(Trim(item));
    if (normalized == normalizedKey || normalized == normalizedName) {
      return true;
    }
  }

  return false;
}

const blazeclaw::config::SkillEntryConfig* ResolveSkillConfig(
    const blazeclaw::config::AppConfig& appConfig,
    const std::wstring& skillKey,
    const std::wstring& skillName) {
  const auto keyIt = appConfig.skills.entries.find(skillKey);
  if (keyIt != appConfig.skills.entries.end()) {
    return &keyIt->second;
  }

  const auto nameIt = appConfig.skills.entries.find(skillName);
  if (nameIt != appConfig.skills.entries.end()) {
    return &nameIt->second;
  }

  return nullptr;
}

} // namespace

std::wstring SkillsEligibilityService::ResolveSkillKey(
    const SkillsCatalogEntry& entry) {
  const auto direct = GetFrontmatterField(
      entry.frontmatter,
      {L"skillkey", L"skill-key", L"openclaw.skillkey"});
  if (!Trim(direct).empty()) {
    return Trim(direct);
  }

  return entry.skillName;
}

SkillsEligibilitySnapshot SkillsEligibilityService::Evaluate(
    const SkillsCatalogSnapshot& catalog,
    const blazeclaw::config::AppConfig& appConfig) const {
  SkillsEligibilitySnapshot snapshot;
  snapshot.entries.reserve(catalog.entries.size());

  for (const auto& catalogEntry : catalog.entries) {
    SkillsEligibilityEntry result;
    result.skillName = catalogEntry.skillName;
    result.skillKey = ResolveSkillKey(catalogEntry);

    const auto* skillConfig = ResolveSkillConfig(
        appConfig,
        result.skillKey,
        result.skillName);

    if (skillConfig != nullptr && skillConfig->enabled.has_value() &&
        !skillConfig->enabled.value()) {
      result.disabled = true;
      ++snapshot.disabledCount;
    }

    result.userInvocable = ParseBoolField(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"user-invocable", L"user_invocable"}),
        true);
    result.disableModelInvocation = ParseBoolField(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"disable-model-invocation", L"disable_model_invocation"}),
        false);

    if (!IsBundledAllowed(catalogEntry, result, appConfig)) {
      result.blockedByAllowlist = true;
      ++snapshot.blockedByAllowlistCount;
    }

    const auto requiredOs = SplitList(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"openclaw.os", L"os"}));
    if (!IsCurrentPlatformAllowed(requiredOs)) {
      result.missingOs = requiredOs;
    }

    const auto requiredBins = SplitList(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"openclaw.requires.bins", L"requires.bins"}));
    for (const auto& bin : requiredBins) {
      if (!HasBinary(bin)) {
        result.missingBins.push_back(bin);
      }
    }

    const auto requiredAnyBins = SplitList(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"openclaw.requires.anybins", L"requires.anybins"}));
    if (!requiredAnyBins.empty()) {
      bool foundAny = false;
      for (const auto& bin : requiredAnyBins) {
        if (HasBinary(bin)) {
          foundAny = true;
          break;
        }
      }

      if (!foundAny) {
        result.missingAnyBins = requiredAnyBins;
      }
    }

    const auto primaryEnv = ToLower(Trim(GetFrontmatterField(
        catalogEntry.frontmatter,
        {L"openclaw.primaryenv", L"primaryenv", L"primary-env"})));
    const auto requiredEnv = SplitList(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"openclaw.requires.env", L"requires.env"}));
    for (const auto& envName : requiredEnv) {
      const std::wstring envWide = envName;
      bool hasValue = HasEnvironmentValue(envWide);
      if (!hasValue && skillConfig != nullptr) {
        const auto envIt = skillConfig->env.find(envWide);
        if (envIt != skillConfig->env.end() && !Trim(envIt->second).empty()) {
          hasValue = true;
        }

        if (!hasValue && envName == primaryEnv &&
            !Trim(skillConfig->apiKey).empty()) {
          hasValue = true;
        }
      }

      if (!hasValue) {
        result.missingEnv.push_back(envName);
      }
    }

    const auto requiredConfig = SplitList(
        GetFrontmatterField(
            catalogEntry.frontmatter,
            {L"openclaw.requires.config", L"requires.config"}));
    for (const auto& configPath : requiredConfig) {
      if (!IsConfigPathTruthy(
              appConfig,
              std::wstring(configPath.begin(), configPath.end()))) {
        result.missingConfig.push_back(configPath);
      }
    }

    const bool hasMissingRequirements =
        !result.missingOs.empty() ||
        !result.missingBins.empty() ||
        !result.missingAnyBins.empty() ||
        !result.missingEnv.empty() ||
        !result.missingConfig.empty();

    if (hasMissingRequirements) {
      ++snapshot.missingRequirementsCount;
    }

    result.eligible =
        !result.disabled &&
        !result.blockedByAllowlist &&
        !hasMissingRequirements;
    if (result.eligible) {
      ++snapshot.eligibleCount;
    }

    snapshot.entries.push_back(std::move(result));
  }

  return snapshot;
}

bool SkillsEligibilityService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  const auto root = fixturesRoot / L"s2-eligibility" / L"workspace";
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.allowBundled = {L"allowed-bundled"};
  appConfig.skills.load.extraDirs = {L"../extra"};
  appConfig.skills.limits.maxCandidatesPerRoot = 32;
  appConfig.skills.limits.maxSkillsLoadedPerSource = 32;
  appConfig.skills.entries[L"disabled-skill"].enabled = false;

  const SkillsCatalogService catalogService;
  const auto catalog = catalogService.LoadCatalog(root, appConfig);
  const auto eligibility = Evaluate(catalog, appConfig);

  const auto findByName = [&eligibility](const std::wstring& name) {
    return std::find_if(
        eligibility.entries.begin(),
        eligibility.entries.end(),
        [&name](const SkillsEligibilityEntry& entry) {
          return ToLower(Trim(entry.skillName)) == ToLower(Trim(name));
        });
  };

  const auto allowed = findByName(L"allowed-bundled");
  if (allowed == eligibility.entries.end() || !allowed->eligible) {
    outError = L"S2 eligibility fixture failed: allowed-bundled should be eligible.";
    return false;
  }

  const auto blocked = findByName(L"blocked-bundled");
  if (blocked == eligibility.entries.end() || !blocked->blockedByAllowlist) {
    outError = L"S2 eligibility fixture failed: blocked-bundled should be blocked by allowlist.";
    return false;
  }

  const auto disabled = findByName(L"disabled-skill");
  if (disabled == eligibility.entries.end() || !disabled->disabled) {
    outError = L"S2 eligibility fixture failed: disabled-skill should be disabled by config.";
    return false;
  }

  const auto envRequired = findByName(L"env-required-skill");
  if (envRequired == eligibility.entries.end() || envRequired->missingEnv.empty()) {
    outError = L"S2 eligibility fixture failed: env-required-skill should have missing env requirement.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
