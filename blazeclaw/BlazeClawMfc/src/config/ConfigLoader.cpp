#include "pch.h"
#include "ConfigLoader.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace blazeclaw::config {

namespace {

std::wstring Trim(const std::wstring& value) {
  const auto first = std::find_if_not(
      value.begin(),
      value.end(),
      [](wchar_t ch) { return std::iswspace(ch) != 0; });

  const auto last = std::find_if_not(
      value.rbegin(),
      value.rend(),
      [](wchar_t ch) { return std::iswspace(ch) != 0; })
                        .base();

  if (first >= last) {
    return {};
  }

  return std::wstring(first, last);
}

bool ParseBool(const std::wstring& raw, const bool fallback) {
  const std::wstring value = Trim(raw);
  if (value == L"true" || value == L"1" || value == L"yes") {
    return true;
  }

  if (value == L"false" || value == L"0" || value == L"no") {
    return false;
  }

  return fallback;
}

bool TryParseUInt(const std::wstring& raw, std::uint32_t& outValue) {
  try {
    const auto parsed = std::stoul(Trim(raw));
    outValue = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace

bool ConfigLoader::LoadFromFile(const std::wstring& path, AppConfig& outConfig) const {
  std::wifstream input(path);
  if (!input.is_open()) {
    return false;
  }

  std::wstring line;
  while (std::getline(input, line)) {
    const std::wstring trimmedLine = Trim(line);
    if (trimmedLine.empty() || trimmedLine.starts_with(L"#")) {
      continue;
    }

    if (trimmedLine.rfind(L"channel=", 0) == 0) {
      outConfig.enabledChannels.push_back(Trim(trimmedLine.substr(8)));
      continue;
    }

    if (trimmedLine.rfind(L"gateway.port=", 0) == 0) {
      try {
        outConfig.gateway.port =
            static_cast<std::uint16_t>(std::stoi(Trim(trimmedLine.substr(13))));
      } catch (...) {
      }
      continue;
    }

    if (trimmedLine.rfind(L"gateway.bind=", 0) == 0) {
      outConfig.gateway.bindAddress = Trim(trimmedLine.substr(13));
      continue;
    }

    if (trimmedLine.rfind(L"agent.model=", 0) == 0) {
      outConfig.agent.model = Trim(trimmedLine.substr(12));
      continue;
    }

    if (trimmedLine.rfind(L"agent.streaming=", 0) == 0) {
      outConfig.agent.enableStreaming = ParseBool(trimmedLine.substr(16), true);
      continue;
    }

    if (trimmedLine.rfind(L"skills.allowBundled=", 0) == 0) {
      const auto skillName = Trim(trimmedLine.substr(20));
      if (!skillName.empty()) {
        outConfig.skills.allowBundled.push_back(skillName);
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.load.extraDir=", 0) == 0) {
      const auto extraDir = Trim(trimmedLine.substr(21));
      if (!extraDir.empty()) {
        outConfig.skills.load.extraDirs.push_back(extraDir);
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.load.watch=", 0) == 0) {
      outConfig.skills.load.watch = ParseBool(trimmedLine.substr(18), true);
      continue;
    }

    if (trimmedLine.rfind(L"skills.load.watchDebounceMs=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(28), value)) {
        outConfig.skills.load.watchDebounceMs = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.limits.maxCandidatesPerRoot=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(33), value)) {
        outConfig.skills.limits.maxCandidatesPerRoot = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.limits.maxSkillsLoadedPerSource=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(36), value)) {
        outConfig.skills.limits.maxSkillsLoadedPerSource = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.limits.maxSkillsInPrompt=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(31), value)) {
        outConfig.skills.limits.maxSkillsInPrompt = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.limits.maxSkillsPromptChars=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(34), value)) {
        outConfig.skills.limits.maxSkillsPromptChars = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.limits.maxSkillFileBytes=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(32), value)) {
        outConfig.skills.limits.maxSkillFileBytes = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.install.preferBrew=", 0) == 0) {
      outConfig.skills.install.preferBrew = ParseBool(trimmedLine.substr(26), true);
      continue;
    }

    if (trimmedLine.rfind(L"skills.install.nodeManager=", 0) == 0) {
      const auto nodeManager = Trim(trimmedLine.substr(27));
      if (!nodeManager.empty()) {
        outConfig.skills.install.nodeManager = nodeManager;
      }
      continue;
    }

    if (trimmedLine.rfind(L"skills.entries.", 0) == 0) {
      const auto keyValuePos = trimmedLine.find(L'=');
      if (keyValuePos == std::wstring::npos) {
        continue;
      }

      const std::wstring keyPath = trimmedLine.substr(15, keyValuePos - 15);
      const std::wstring value = Trim(trimmedLine.substr(keyValuePos + 1));
      const auto fieldDot = keyPath.find(L'.');
      if (fieldDot == std::wstring::npos) {
        continue;
      }

      const std::wstring skillKey = Trim(keyPath.substr(0, fieldDot));
      const std::wstring fieldPath = Trim(keyPath.substr(fieldDot + 1));
      if (skillKey.empty() || fieldPath.empty()) {
        continue;
      }

      auto& entry = outConfig.skills.entries[skillKey];
      if (fieldPath == L"enabled") {
        entry.enabled = ParseBool(value, true);
        continue;
      }

      if (fieldPath == L"apiKey") {
        entry.apiKey = value;
        continue;
      }

      if (fieldPath.rfind(L"env.", 0) == 0) {
        const auto envName = Trim(fieldPath.substr(4));
        if (!envName.empty()) {
          entry.env[envName] = value;
        }
      }
    }
  }

  return true;
}

} // namespace blazeclaw::config
