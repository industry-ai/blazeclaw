#include "pch.h"
#include "SkillSecurityScanService.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <regex>
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

std::optional<std::wstring> ReadUtf8(const std::filesystem::path& pathValue) {
  std::ifstream input(pathValue, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::string content(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());

  if (content.empty()) {
    return std::wstring();
  }

  const int required = MultiByteToWideChar(
      CP_UTF8,
      0,
      content.c_str(),
      static_cast<int>(content.size()),
      nullptr,
      0);
  if (required <= 0) {
    return std::wstring(content.begin(), content.end());
  }

  std::wstring output(static_cast<std::size_t>(required), L'\0');
  MultiByteToWideChar(
      CP_UTF8,
      0,
      content.c_str(),
      static_cast<int>(content.size()),
      output.data(),
      required);
  return output;
}

bool IsScannable(const std::filesystem::path& filePath) {
  const auto ext = ToLower(filePath.extension().wstring());
  static const std::vector<std::wstring> supported = {
      L".js", L".ts", L".mjs", L".cjs", L".ps1", L".py", L".sh", L".bat", L".cmd"};
  return std::find(supported.begin(), supported.end(), ext) != supported.end();
}

} // namespace

SkillSecurityScanSnapshot SkillSecurityScanService::BuildSnapshot(
    const SkillsCatalogSnapshot& catalog,
    const SkillsEligibilitySnapshot& eligibility,
    const blazeclaw::config::AppConfig& appConfig) const {
  (void)appConfig;

  SkillSecurityScanSnapshot snapshot;

  std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
  for (const auto& item : eligibility.entries) {
    eligibilityByName.emplace(ToLower(item.skillName), item);
  }

  struct ScanRule {
    std::wstring id;
    std::wstring severity;
    std::wregex pattern;
  };

  const std::vector<ScanRule> rules = {
      {L"dangerous-exec", L"critical", std::wregex(LR"((execSync|spawnSync|eval\s*\())")},
      {L"env-harvest", L"critical", std::wregex(LR"((process\.env|_wgetenv|_wdupenv_s).*(http|fetch|post))")},
      {L"suspicious-network", L"warn", std::wregex(LR"((WebSocket\s*\(|wss?:\/\/))")},
      {L"obfuscated-payload", L"warn", std::wregex(LR"((\\x[0-9A-Fa-f]{2}{4,}|base64))")},
  };

  for (const auto& entry : catalog.entries) {
    const auto eligibilityIt = eligibilityByName.find(ToLower(entry.skillName));
    if (eligibilityIt == eligibilityByName.end() || !eligibilityIt->second.eligible) {
      continue;
    }

    std::error_code ec;
    for (const auto& file : std::filesystem::recursive_directory_iterator(entry.skillDir, ec)) {
      if (ec) {
        break;
      }

      if (!file.is_regular_file()) {
        continue;
      }

      if (!IsScannable(file.path())) {
        continue;
      }

      ++snapshot.scannedFileCount;
      const auto content = ReadUtf8(file.path());
      if (!content.has_value()) {
        continue;
      }

      for (const auto& rule : rules) {
        std::wsmatch match;
        if (!std::regex_search(content.value(), match, rule.pattern)) {
          continue;
        }

        SkillScanFinding finding;
        finding.skillName = entry.skillName;
        finding.file = file.path().wstring();
        finding.ruleId = rule.id;
        finding.severity = rule.severity;
        finding.evidence = match.str();
        snapshot.findings.push_back(std::move(finding));

        if (rule.severity == L"critical") {
          ++snapshot.criticalCount;
        } else if (rule.severity == L"warn") {
          ++snapshot.warnCount;
        } else {
          ++snapshot.infoCount;
        }
      }
    }
  }

  return snapshot;
}

bool SkillSecurityScanService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  const auto root = fixturesRoot / L"s5-scan" / L"workspace";
  blazeclaw::config::AppConfig appConfig;

  const SkillsCatalogService catalogService;
  const SkillsEligibilityService eligibilityService;
  const auto catalog = catalogService.LoadCatalog(root, appConfig);
  const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
  const auto snapshot = BuildSnapshot(catalog, eligibility, appConfig);

  if (snapshot.criticalCount == 0 || snapshot.warnCount == 0) {
    outError = L"S5 scanner fixture failed: expected critical and warning findings.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
