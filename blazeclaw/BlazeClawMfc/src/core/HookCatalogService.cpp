#include "pch.h"
#include "HookCatalogService.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

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

std::wstring TrimQuotes(const std::wstring& value) {
  const std::wstring trimmed = Trim(value);
  if (trimmed.size() >= 2) {
    const wchar_t first = trimmed.front();
    const wchar_t last = trimmed.back();
    if ((first == L'"' && last == L'"') ||
        (first == L'\'' && last == L'\'')) {
      return trimmed.substr(1, trimmed.size() - 2);
    }
  }

  return trimmed;
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

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int required = MultiByteToWideChar(
      CP_UTF8,
      0,
      value.c_str(),
      static_cast<int>(value.size()),
      nullptr,
      0);
  if (required <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  std::wstring output(static_cast<std::size_t>(required), L'\0');
  const int converted = MultiByteToWideChar(
      CP_UTF8,
      0,
      value.c_str(),
      static_cast<int>(value.size()),
      output.data(),
      required);
  if (converted <= 0) {
    return std::wstring(value.begin(), value.end());
  }

  return output;
}

std::optional<std::wstring> ReadFileUtf8(const std::filesystem::path& filePath) {
  std::ifstream input(filePath, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::string content(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  return Utf8ToWide(content);
}

std::filesystem::path CanonicalOrSelf(const std::filesystem::path& pathValue) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(pathValue, ec);
  if (ec) {
    return pathValue.lexically_normal();
  }

  return canonical;
}

bool IsPathInside(
    const std::filesystem::path& rootPath,
    const std::filesystem::path& candidatePath) {
  const auto canonicalRoot = CanonicalOrSelf(rootPath);
  const auto canonicalCandidate = CanonicalOrSelf(candidatePath);

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

bool IsUnsafeRelativePath(const std::wstring& value) {
  const std::wstring trimmed = Trim(value);
  if (trimmed.empty()) {
    return true;
  }

  if (trimmed.find(L"..") != std::wstring::npos) {
    return true;
  }

  if (trimmed.find(L'\\') != std::wstring::npos) {
    return true;
  }

  if (trimmed.find(L':') != std::wstring::npos) {
    return true;
  }

  if (!trimmed.empty() && trimmed.front() == L'/') {
    return true;
  }

  return false;
}

} // namespace

std::optional<HookFrontmatter> HookCatalogService::ParseFrontmatter(
    const std::wstring& hookContent,
    std::vector<std::wstring>& outValidationErrors) {
  std::vector<std::wstring> lines;
  std::size_t cursor = 0;
  while (cursor <= hookContent.size()) {
    const auto next = hookContent.find(L'\n', cursor);
    if (next == std::wstring::npos) {
      lines.push_back(hookContent.substr(cursor));
      break;
    }

    lines.push_back(hookContent.substr(cursor, next - cursor));
    cursor = next + 1;
  }

  if (lines.empty() || Trim(lines[0]) != L"---") {
    outValidationErrors.push_back(
        L"Missing frontmatter start marker (---).");
    return std::nullopt;
  }

  HookFrontmatter parsed;
  bool closed = false;
  for (std::size_t lineIndex = 1; lineIndex < lines.size(); ++lineIndex) {
    const std::wstring line = Trim(lines[lineIndex]);
    if (line == L"---") {
      closed = true;
      break;
    }

    if (line.empty() || line.starts_with(L"#")) {
      continue;
    }

    const auto colonPos = line.find(L':');
    if (colonPos == std::wstring::npos) {
      outValidationErrors.push_back(L"Invalid frontmatter line: " + line);
      continue;
    }

    const std::wstring key = ToLower(Trim(line.substr(0, colonPos)));
    const std::wstring value = TrimQuotes(line.substr(colonPos + 1));
    if (key == L"name") {
      parsed.name = value;
    } else if (key == L"description") {
      parsed.description = value;
    } else if (key == L"blazeclaw.event" || key == L"event") {
      parsed.eventName = value;
    } else if (key == L"blazeclaw.handler" || key == L"handler") {
      parsed.handlerPath = value;
    }
  }

  if (!closed) {
    outValidationErrors.push_back(
        L"Missing frontmatter closing marker (---).");
  }

  if (Trim(parsed.name).empty()) {
    outValidationErrors.push_back(L"Missing required frontmatter field: name.");
  }

  if (Trim(parsed.description).empty()) {
    outValidationErrors.push_back(
        L"Missing required frontmatter field: description.");
  }

  if (Trim(parsed.eventName).empty()) {
    outValidationErrors.push_back(
        L"Missing required frontmatter field: blazeclaw.event.");
  }

  if (Trim(parsed.handlerPath).empty()) {
    outValidationErrors.push_back(
        L"Missing required frontmatter field: blazeclaw.handler.");
  }

  if (!outValidationErrors.empty()) {
    return std::nullopt;
  }

  return parsed;
}

HookCatalogSnapshot HookCatalogService::BuildSnapshot(
    const SkillsCatalogSnapshot& catalog) const {
  HookCatalogSnapshot snapshot;

  for (const auto& skill : catalog.entries) {
    const auto hookFile = skill.skillDir / L"hooks" / L"blazeclaw" / L"HOOK.md";
    std::error_code hookEc;
    if (!std::filesystem::is_regular_file(hookFile, hookEc) || hookEc) {
      continue;
    }

    HookCatalogEntry entry;
    entry.skillName = skill.skillName;
    entry.hookFile = hookFile;

    const auto content = ReadFileUtf8(hookFile);
    if (!content.has_value()) {
      ++snapshot.diagnostics.invalidMetadataFiles;
      entry.validationErrors.push_back(L"Failed to read HOOK.md.");
      snapshot.entries.push_back(std::move(entry));
      continue;
    }

    std::vector<std::wstring> validationErrors;
    const auto frontmatter = ParseFrontmatter(content.value(), validationErrors);
    if (!frontmatter.has_value()) {
      ++snapshot.diagnostics.invalidMetadataFiles;
      entry.validationErrors = validationErrors;
      snapshot.entries.push_back(std::move(entry));
      continue;
    }

    entry.frontmatter = frontmatter.value();
    entry.validMetadata = true;

    const std::wstring handlerPathText = frontmatter->handlerPath;
    if (IsUnsafeRelativePath(handlerPathText)) {
      ++snapshot.diagnostics.unsafeHandlerPaths;
      entry.validationErrors.push_back(L"Unsafe handler path.");
      snapshot.entries.push_back(std::move(entry));
      continue;
    }

    entry.handlerFile = skill.skillDir / std::filesystem::path(handlerPathText);
    if (!IsPathInside(skill.skillDir, entry.handlerFile)) {
      ++snapshot.diagnostics.unsafeHandlerPaths;
      entry.validationErrors.push_back(
          L"Handler path escapes skill directory.");
      snapshot.entries.push_back(std::move(entry));
      continue;
    }

    entry.safeHandlerPath = true;

    std::error_code handlerEc;
    entry.handlerExists =
        std::filesystem::is_regular_file(entry.handlerFile, handlerEc) &&
        !handlerEc;
    if (!entry.handlerExists) {
      ++snapshot.diagnostics.missingHandlerFiles;
      entry.validationErrors.push_back(L"Hook handler file missing.");
      snapshot.entries.push_back(std::move(entry));
      continue;
    }

    ++snapshot.diagnostics.hooksLoaded;
    snapshot.entries.push_back(std::move(entry));
  }

  std::sort(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const HookCatalogEntry& left, const HookCatalogEntry& right) {
        return ToLower(left.skillName) < ToLower(right.skillName);
      });

  return snapshot;
}

bool HookCatalogService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  const auto root = fixturesRoot / L"s8-hooks" / L"workspace";
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.limits.maxCandidatesPerRoot = 32;
  appConfig.skills.limits.maxSkillsLoadedPerSource = 32;
  appConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

  const SkillsCatalogService catalogService;
  const auto catalog = catalogService.LoadCatalog(root, appConfig);
  const auto snapshot = BuildSnapshot(catalog);

  const auto validIt = std::find_if(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const HookCatalogEntry& entry) {
        return ToLower(Trim(entry.skillName)) == L"hook-valid";
      });
  if (validIt == snapshot.entries.end()) {
    outError = L"S8 hooks fixture failed: expected hook-valid entry.";
    return false;
  }

  if (!validIt->validMetadata || !validIt->safeHandlerPath ||
      !validIt->handlerExists) {
    outError =
        L"S8 hooks fixture failed: hook-valid should pass metadata/path checks.";
    return false;
  }

  if (snapshot.diagnostics.invalidMetadataFiles == 0) {
    outError =
        L"S8 hooks fixture failed: expected invalid metadata diagnostics.";
    return false;
  }

  if (snapshot.diagnostics.unsafeHandlerPaths == 0) {
    outError =
        L"S8 hooks fixture failed: expected unsafe handler path diagnostics.";
    return false;
  }

  if (snapshot.diagnostics.hooksLoaded == 0) {
    outError =
        L"S8 hooks fixture failed: expected at least one loadable hook.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
