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

bool TryParseDouble(const std::wstring& raw, double& outValue) {
  try {
    outValue = std::stod(Trim(raw));
    return true;
  } catch (...) {
    return false;
  }
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

std::wstring NormalizeChatUiMode(const std::wstring& raw) {
  std::wstring normalized;
  normalized.reserve(raw.size());
  for (const wchar_t ch : raw) {
    normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
  }

  normalized = Trim(normalized);
  if (normalized == L"native") {
    return normalized;
  }

  return L"webview2";
}

std::wstring ToLowerTrim(const std::wstring& raw) {
  const std::wstring trimmed = Trim(raw);
  std::wstring normalized;
  normalized.reserve(trimmed.size());
  for (const wchar_t ch : trimmed) {
    normalized.push_back(
        static_cast<wchar_t>(std::towlower(ch)));
  }

  return normalized;
}

std::wstring NormalizeEmbeddingsProvider(const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized.empty()) {
    return L"onnx";
  }

  if (normalized == L"onnx") {
    return normalized;
  }

  return L"onnx";
}

std::wstring NormalizeLocalModelRolloutStage(
    const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized == L"dev" ||
      normalized == L"nightly" ||
      normalized == L"stable") {
    return normalized;
  }

  return L"dev";
}

std::wstring NormalizeLocalModelExecutionMode(
    const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized == L"parallel") {
    return normalized;
  }

  return L"sequential";
}

std::wstring NormalizeReminderVerbosity(const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized == L"minimal" ||
      normalized == L"normal" ||
      normalized == L"detailed") {
    return normalized;
  }

  return L"normal";
}

std::wstring NormalizeEmbeddingsExecutionMode(
    const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized == L"parallel") {
    return normalized;
  }

  return L"sequential";
}

std::wstring NormalizeLocalModelProvider(const std::wstring& raw) {
  const std::wstring normalized = ToLowerTrim(raw);
  if (normalized.empty()) {
    return L"onnx";
  }

  if (normalized == L"onnx") {
    return normalized;
  }

  return L"onnx";
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

    if (trimmedLine.rfind(L"chat.ui.mode=", 0) == 0) {
      outConfig.chat.mode =
          NormalizeChatUiMode(trimmedLine.substr(13));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.enabled=", 0) == 0) {
      outConfig.localModel.enabled = ParseBool(
          trimmedLine.substr(24),
          false);
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.provider=", 0) == 0) {
      outConfig.localModel.provider = NormalizeLocalModelProvider(
          trimmedLine.substr(25));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.rolloutStage=", 0) == 0) {
      outConfig.localModel.rolloutStage =
          NormalizeLocalModelRolloutStage(
              trimmedLine.substr(29));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.storageRoot=", 0) == 0) {
      outConfig.localModel.storageRoot = Trim(
          trimmedLine.substr(28));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.version=", 0) == 0) {
      outConfig.localModel.version = Trim(
          trimmedLine.substr(24));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.modelPath=", 0) == 0) {
      outConfig.localModel.modelPath = Trim(
          trimmedLine.substr(26));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.modelSha256=", 0) == 0) {
      outConfig.localModel.modelSha256 = ToLowerTrim(
          trimmedLine.substr(28));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.tokenizerPath=", 0) == 0) {
      outConfig.localModel.tokenizerPath = Trim(
          trimmedLine.substr(30));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.tokenizerSha256=", 0) == 0) {
      outConfig.localModel.tokenizerSha256 = ToLowerTrim(
          trimmedLine.substr(32));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.maxTokens=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(26), value) && value > 0) {
        outConfig.localModel.maxTokens = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.temperature=", 0) == 0) {
      double value = 0.0;
      if (TryParseDouble(trimmedLine.substr(28), value) && value >= 0.0) {
        outConfig.localModel.temperature = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.intraThreads=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(29), value)) {
        outConfig.localModel.intraThreads = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.interThreads=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(29), value)) {
        outConfig.localModel.interThreads = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.executionMode=", 0) == 0) {
      outConfig.localModel.executionMode = NormalizeLocalModelExecutionMode(
          trimmedLine.substr(30));
      continue;
    }

    if (trimmedLine.rfind(L"chat.localModel.verboseMetrics=", 0) == 0) {
      outConfig.localModel.verboseMetrics = ParseBool(
          trimmedLine.substr(31),
          false);
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

    if (trimmedLine.rfind(L"models.primary=", 0) == 0) {
      outConfig.models.primary = Trim(trimmedLine.substr(13));
      continue;
    }

    if (trimmedLine.rfind(L"models.fallback=", 0) == 0) {
      outConfig.models.fallback = Trim(trimmedLine.substr(14));
      continue;
    }

    if (trimmedLine.rfind(L"models.allow=", 0) == 0) {
      const auto value = Trim(trimmedLine.substr(12));
      if (!value.empty()) {
        outConfig.models.allow.push_back(value);
      }

      continue;
    }

    if (trimmedLine.rfind(L"models.alias.", 0) == 0) {
      const auto keyValuePos = trimmedLine.find(L'=');
      if (keyValuePos == std::wstring::npos) {
        continue;
      }

      const std::wstring aliasKey = Trim(trimmedLine.substr(13, keyValuePos - 13));
      const std::wstring aliasTarget = Trim(trimmedLine.substr(keyValuePos + 1));
      if (!aliasKey.empty() && !aliasTarget.empty()) {
        outConfig.models.aliases[aliasKey] = aliasTarget;
      }

      continue;
    }

    if (trimmedLine.rfind(L"models.maxFailoverAttempts=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(26), value)) {
        outConfig.models.maxFailoverAttempts = value;
      }

      continue;
    }

    if (trimmedLine.rfind(L"embeddings.enabled=", 0) == 0) {
      outConfig.embeddings.enabled = ParseBool(
          trimmedLine.substr(19),
          false);
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.provider=", 0) == 0) {
      outConfig.embeddings.provider = NormalizeEmbeddingsProvider(
          trimmedLine.substr(20));
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.model_path=", 0) == 0) {
      outConfig.embeddings.modelPath = Trim(
          trimmedLine.substr(22));
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.tokenizer_path=", 0) == 0) {
      outConfig.embeddings.tokenizerPath = Trim(
          trimmedLine.substr(26));
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.dimension=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(21), value) && value > 0) {
        outConfig.embeddings.dimension = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.max_sequence_length=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(31), value) && value > 0) {
        outConfig.embeddings.maxSequenceLength = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.normalize=", 0) == 0) {
      outConfig.embeddings.normalize = ParseBool(
          trimmedLine.substr(21),
          true);
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.intra_threads=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(25), value)) {
        outConfig.embeddings.intraThreads = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.inter_threads=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(25), value)) {
        outConfig.embeddings.interThreads = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"embeddings.execution_mode=", 0) == 0) {
      outConfig.embeddings.executionMode =
          NormalizeEmbeddingsExecutionMode(
              trimmedLine.substr(26));
      continue;
    }

    if (trimmedLine.rfind(L"auth.order=", 0) == 0) {
      const auto value = Trim(trimmedLine.substr(11));
      if (!value.empty()) {
        outConfig.authProfiles.order.push_back(value);
      }

      continue;
    }

    if (trimmedLine.rfind(L"auth.profiles.", 0) == 0) {
      const auto keyValuePos = trimmedLine.find(L'=');
      if (keyValuePos == std::wstring::npos) {
        continue;
      }

      const std::wstring path = Trim(trimmedLine.substr(14, keyValuePos - 14));
      const std::wstring value = Trim(trimmedLine.substr(keyValuePos + 1));
      const auto parts = Split(path, L'.');
      if (parts.size() < 2) {
        continue;
      }

      const std::wstring profileId = Trim(parts[0]);
      if (profileId.empty()) {
        continue;
      }

      auto& profile = outConfig.authProfiles.entries[profileId];
      profile.id = profileId;
      const std::wstring fieldName = Trim(parts[1]);
      if (fieldName == L"provider") {
        profile.provider = value;
        continue;
      }

      if (fieldName == L"credentialRef") {
        profile.credentialRef = value;
        continue;
      }

      if (fieldName == L"cooldownSeconds") {
        std::uint32_t parsedCooldown = 0;
        if (TryParseUInt(value, parsedCooldown)) {
          profile.cooldownSeconds = parsedCooldown;
        }

        continue;
      }

      if (fieldName == L"enabled") {
        profile.enabled = ParseBool(value, true);
        continue;
      }
    }

    if (trimmedLine.rfind(L"sandbox.enabled=", 0) == 0) {
      outConfig.sandbox.enabled = ParseBool(trimmedLine.substr(16), false);
      continue;
    }

    if (trimmedLine.rfind(L"sandbox.runtime=", 0) == 0) {
      outConfig.sandbox.runtime = Trim(trimmedLine.substr(16));
      continue;
    }

    if (trimmedLine.rfind(L"sandbox.workspaceMirrorRoot=", 0) == 0) {
      outConfig.sandbox.workspaceMirrorRoot = Trim(trimmedLine.substr(28));
      continue;
    }

    if (trimmedLine.rfind(L"sandbox.allowHostNetwork=", 0) == 0) {
      outConfig.sandbox.allowHostNetwork = ParseBool(trimmedLine.substr(25), false);
      continue;
    }

    if (trimmedLine.rfind(L"sandbox.browserEnabled=", 0) == 0) {
      outConfig.sandbox.browserEnabled = ParseBool(trimmedLine.substr(23), false);
      continue;
    }

    if (trimmedLine.rfind(L"transcript.repairEnabled=", 0) == 0) {
      outConfig.transcript.repairEnabled = ParseBool(trimmedLine.substr(25), true);
      continue;
    }

    if (trimmedLine.rfind(L"transcript.writeLockEnabled=", 0) == 0) {
      outConfig.transcript.writeLockEnabled = ParseBool(trimmedLine.substr(28), true);
      continue;
    }

    if (trimmedLine.rfind(L"transcript.redactSecrets=", 0) == 0) {
      outConfig.transcript.redactSecrets = ParseBool(trimmedLine.substr(24), true);
      continue;
    }

    if (trimmedLine.rfind(L"transcript.maxPayloadChars=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(27), value)) {
        outConfig.transcript.maxPayloadChars = value;
      }

      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.enabled=", 0) == 0) {
      outConfig.hooks.engine.enabled =
          ParseBool(trimmedLine.substr(21), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.fallbackPromptInjection=", 0) == 0) {
      outConfig.hooks.engine.fallbackPromptInjection =
          ParseBool(trimmedLine.substr(38), false);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.reminderEnabled=", 0) == 0) {
      outConfig.hooks.engine.reminderEnabled =
          ParseBool(trimmedLine.substr(29), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.reminderVerbosity=", 0) == 0) {
      outConfig.hooks.engine.reminderVerbosity =
          NormalizeReminderVerbosity(trimmedLine.substr(31));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.allowPackage=", 0) == 0) {
      const auto packageName = ToLowerTrim(trimmedLine.substr(25));
      if (!packageName.empty()) {
        outConfig.hooks.engine.allowedPackages.push_back(packageName);
      }
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.strictPolicyEnforcement=", 0) == 0) {
      outConfig.hooks.engine.strictPolicyEnforcement =
          ParseBool(trimmedLine.substr(37), false);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.governanceReportingEnabled=", 0) == 0) {
      outConfig.hooks.engine.governanceReportingEnabled =
          ParseBool(trimmedLine.substr(39), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.governanceReportDir=", 0) == 0) {
      outConfig.hooks.engine.governanceReportDir =
          Trim(trimmedLine.substr(33));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationEnabled=", 0) == 0) {
      outConfig.hooks.engine.autoRemediationEnabled =
          ParseBool(trimmedLine.substr(35), false);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationRequiresApproval=", 0) == 0) {
      outConfig.hooks.engine.autoRemediationRequiresApproval =
          ParseBool(trimmedLine.substr(44), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationApprovalToken=", 0) == 0) {
      outConfig.hooks.engine.autoRemediationApprovalToken =
          Trim(trimmedLine.substr(41));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationTenantId=", 0) == 0) {
      outConfig.hooks.engine.autoRemediationTenantId =
          Trim(trimmedLine.substr(37));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationPlaybookDir=", 0) == 0) {
      outConfig.hooks.engine.autoRemediationPlaybookDir =
          Trim(trimmedLine.substr(40));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.autoRemediationTokenMaxAgeMinutes=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(47), value)) {
        outConfig.hooks.engine.autoRemediationTokenMaxAgeMinutes = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationTelemetryEnabled=", 0) == 0) {
      outConfig.hooks.engine.remediationTelemetryEnabled =
          ParseBool(trimmedLine.substr(40), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationTelemetryDir=", 0) == 0) {
      outConfig.hooks.engine.remediationTelemetryDir =
          Trim(trimmedLine.substr(36));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationAuditEnabled=", 0) == 0) {
      outConfig.hooks.engine.remediationAuditEnabled =
          ParseBool(trimmedLine.substr(36), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationAuditDir=", 0) == 0) {
      outConfig.hooks.engine.remediationAuditDir =
          Trim(trimmedLine.substr(32));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationSloMaxDriftDetected=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(42), value)) {
        outConfig.hooks.engine.remediationSloMaxDriftDetected = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.remediationSloMaxPolicyBlocked=", 0) == 0) {
      std::uint32_t value = 0;
      if (TryParseUInt(trimmedLine.substr(41), value)) {
        outConfig.hooks.engine.remediationSloMaxPolicyBlocked = value;
      }
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.complianceAttestationEnabled=", 0) == 0) {
      outConfig.hooks.engine.complianceAttestationEnabled =
          ParseBool(trimmedLine.substr(40), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.complianceAttestationDir=", 0) == 0) {
      outConfig.hooks.engine.complianceAttestationDir =
          Trim(trimmedLine.substr(36));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.enterpriseSlaGovernanceEnabled=", 0) == 0) {
      outConfig.hooks.engine.enterpriseSlaGovernanceEnabled =
          ParseBool(trimmedLine.substr(42), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.enterpriseSlaPolicyId=", 0) == 0) {
      outConfig.hooks.engine.enterpriseSlaPolicyId =
          Trim(trimmedLine.substr(34));
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.crossTenantAttestationAggregationEnabled=", 0) == 0) {
      outConfig.hooks.engine.crossTenantAttestationAggregationEnabled =
          ParseBool(trimmedLine.substr(52), true);
      continue;
    }

    if (trimmedLine.rfind(L"hooks.engine.crossTenantAttestationAggregationDir=", 0) == 0) {
      outConfig.hooks.engine.crossTenantAttestationAggregationDir =
          Trim(trimmedLine.substr(48));
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

      if (fieldName == L"tools" && parts.size() >= 3) {
        const std::wstring toolField = Trim(parts[2]);
        if (toolField == L"profile") {
          if (!value.empty()) {
            entry.tools.profile = value;
          }

          continue;
        }

        if (toolField == L"allowTool") {
          if (!value.empty()) {
            entry.tools.allow.push_back(value);
          }

          continue;
        }

        if (toolField == L"denyTool") {
          if (!value.empty()) {
            entry.tools.deny.push_back(value);
          }

          continue;
        }

        if (toolField == L"ownerOnlyTool") {
          if (!value.empty()) {
            entry.tools.ownerOnly.push_back(value);
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

  outConfig.embeddings.provider = NormalizeEmbeddingsProvider(
      outConfig.embeddings.provider);
  outConfig.embeddings.executionMode = NormalizeEmbeddingsExecutionMode(
      outConfig.embeddings.executionMode);
  outConfig.localModel.provider = NormalizeLocalModelProvider(
      outConfig.localModel.provider);
  if (outConfig.embeddings.dimension == 0) {
    outConfig.embeddings.dimension = 384;
  }

  if (outConfig.embeddings.maxSequenceLength == 0) {
    outConfig.embeddings.maxSequenceLength = 256;
  }

  if (outConfig.localModel.maxTokens == 0) {
    outConfig.localModel.maxTokens = 256;
  }

  return true;
}

} // namespace blazeclaw::config
