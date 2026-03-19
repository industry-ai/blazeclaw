#include "pch.h"
#include "ConfigLoader.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <vector>

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

std::wstring NormalizeAgentId(const std::wstring& raw) {
  std::wstring normalized;
  normalized.reserve(raw.size());
  for (const wchar_t ch : raw) {
    const wchar_t lowered = static_cast<wchar_t>(std::towlower(ch));
    if ((lowered >= L'a' && lowered <= L'z') ||
        (lowered >= L'0' && lowered <= L'9') ||
        lowered == L'-' || lowered == L'_') {
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

    if (trimmedLine.rfind(L"acp.enabled=", 0) == 0) {
      outConfig.acp.enabled = ParseBool(trimmedLine.substr(12), false);
      continue;
    }

    if (trimmedLine.rfind(L"acp.defaultAgent=", 0) == 0) {
      outConfig.acp.defaultAgent = NormalizeAgentId(Trim(trimmedLine.substr(17)));
      continue;
    }

    if (trimmedLine.rfind(L"acp.allowThreadSpawn=", 0) == 0) {
      outConfig.acp.allowThreadSpawn = ParseBool(trimmedLine.substr(21), true);
      continue;
    }

    if (trimmedLine.rfind(L"embedded.enabled=", 0) == 0) {
      outConfig.embedded.enabled = ParseBool(trimmedLine.substr(17), true);
      continue;
    }

    if (trimmedLine.rfind(L"embedded.runTimeoutMs=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(22), value)) {
        outConfig.embedded.runTimeoutMs = value;
      }

      continue;
    }

    if (trimmedLine.rfind(L"embedded.maxQueueDepth=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(23), value)) {
        outConfig.embedded.maxQueueDepth = value;
      }

      continue;
    }

    if (trimmedLine.rfind(L"agents.defaults.", 0) == 0) {
      const auto keyValuePos = trimmedLine.find(L'=');
      if (keyValuePos == std::wstring::npos) {
        continue;
      }

      const std::wstring fieldName =
          Trim(trimmedLine.substr(16, keyValuePos - 16));
      const std::wstring value = Trim(trimmedLine.substr(keyValuePos + 1));
      if (fieldName == L"id") {
        outConfig.agents.defaults.agentId = NormalizeAgentId(value);
        continue;
      }

      if (fieldName == L"workspace") {
        outConfig.agents.defaults.workspace = value;
        continue;
      }

      if (fieldName == L"workspaceRoot") {
        outConfig.agents.defaults.workspaceRoot = value;
        continue;
      }

      if (fieldName == L"agentDirRoot") {
        outConfig.agents.defaults.agentDirRoot = value;
        continue;
      }

      if (fieldName == L"model") {
        outConfig.agents.defaults.model = value;
      }

      continue;
    }

    if (trimmedLine.rfind(L"agents.list.", 0) == 0) {
      const auto keyValuePos = trimmedLine.find(L'=');
      if (keyValuePos == std::wstring::npos) {
        continue;
      }

      const std::wstring path = Trim(trimmedLine.substr(12, keyValuePos - 12));
      const std::wstring value = Trim(trimmedLine.substr(keyValuePos + 1));
      const auto parts = Split(path, L'.');
      if (parts.size() < 2) {
        continue;
      }

      const std::wstring entryId = NormalizeAgentId(Trim(parts[0]));
      if (entryId.empty()) {
        continue;
      }

      auto& entry = outConfig.agents.entries[entryId];
      entry.id = entryId;

      const std::wstring fieldName = Trim(parts[1]);
      if (fieldName == L"name") {
        entry.name = value;
        continue;
      }

      if (fieldName == L"workspace") {
        entry.workspace = value;
        continue;
      }

      if (fieldName == L"agentDir") {
        entry.agentDir = value;
        continue;
      }

      if (fieldName == L"model") {
        entry.model = value;
        continue;
      }

      if (fieldName == L"default") {
        entry.isDefault = ParseBool(value, false);
        continue;
      }

      if (fieldName == L"identity" && parts.size() >= 3) {
        const std::wstring identityField = Trim(parts[2]);
        if (identityField == L"name") {
          entry.identity.name = value;
          continue;
        }

        if (identityField == L"emoji") {
          entry.identity.emoji = value;
          continue;
        }

        if (identityField == L"theme") {
          entry.identity.theme = value;
          continue;
        }

        if (identityField == L"avatar") {
          entry.identity.avatar = value;
        }

        continue;
      }

      if (fieldName == L"subagents" && parts.size() >= 3) {
        const std::wstring subField = Trim(parts[2]);
        if (subField == L"maxDepth") {
          std::uint32_t parsedDepth = 0;
          if (TryParseUInt(value, parsedDepth)) {
            entry.subagents.maxDepth = parsedDepth;
          }

          continue;
        }

        if (subField == L"allowAgent") {
          const auto allowAgentId = NormalizeAgentId(value);
          if (!allowAgentId.empty()) {
            entry.subagents.allowAgents.push_back(allowAgentId);
          }

          continue;
        }
      }

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

  if (outConfig.agents.defaults.model.empty()) {
    outConfig.agents.defaults.model = outConfig.agent.model;
  }

  return true;
}

} // namespace blazeclaw::config
