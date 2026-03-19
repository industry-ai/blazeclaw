#include "pch.h"
#include "AgentsTranscriptSafetyService.h"

namespace blazeclaw::core {

void AgentsTranscriptSafetyService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
}

std::string AgentsTranscriptSafetyService::TruncateUtf8(
    const std::string& value,
    const std::size_t maxChars) {
  if (value.size() <= maxChars) {
    return value;
  }

  return value.substr(0, maxChars);
}

std::string AgentsTranscriptSafetyService::RedactSecrets(
    const std::string& value) {
  std::string redacted = value;

  const std::vector<std::pair<std::string, std::string>> masks = {
      {"api_key", "[REDACTED_KEY]"},
      {"token", "[REDACTED_TOKEN]"},
      {"password", "[REDACTED_PASSWORD]"},
      {"secret", "[REDACTED_SECRET]"},
  };

  for (const auto& [needle, replacement] : masks) {
    std::size_t cursor = 0;
    while (cursor < redacted.size()) {
      const auto hit = redacted.find(needle, cursor);
      if (hit == std::string::npos) {
        break;
      }

      redacted.replace(hit, needle.size(), replacement);
      cursor = hit + replacement.size();
    }
  }

  return redacted;
}

TranscriptSafetyResult AgentsTranscriptSafetyService::GuardPayload(
    const std::string& payload,
    const bool hasWriteLock,
    const bool toolResultPayload) const {
  TranscriptSafetyResult result;
  result.payload = payload;

  if (m_config.transcript.writeLockEnabled && hasWriteLock) {
    result.accepted = false;
    result.writeLocked = true;
    result.warnings.push_back("write_lock_active");
    return result;
  }

  if (m_config.transcript.redactSecrets) {
    const auto sanitized = RedactSecrets(result.payload);
    if (sanitized != result.payload) {
      result.redacted = true;
      result.payload = sanitized;
      result.warnings.push_back("payload_redacted");
    }
  }

  const std::size_t maxChars = static_cast<std::size_t>(
      m_config.transcript.maxPayloadChars == 0
      ? 1
      : m_config.transcript.maxPayloadChars);

  if (result.payload.size() > maxChars) {
    if (m_config.transcript.repairEnabled) {
      result.payload = TruncateUtf8(result.payload, maxChars);
      result.repaired = true;
      result.warnings.push_back("payload_truncated");
    } else {
      result.accepted = false;
      result.warnings.push_back("payload_exceeds_limit");
      return result;
    }
  }

  if (toolResultPayload && result.payload.find("tool_result") == std::string::npos) {
    result.warnings.push_back("tool_result_guard_applied");
  }

  return result;
}

bool AgentsTranscriptSafetyService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;
  config.transcript.repairEnabled = true;
  config.transcript.writeLockEnabled = true;
  config.transcript.redactSecrets = true;
  config.transcript.maxPayloadChars = 16;

  AgentsTranscriptSafetyService service;
  service.Configure(config);

  const auto locked = service.GuardPayload("hello", true, false);
  if (locked.accepted || !locked.writeLocked) {
    outError = L"Fixture validation failed: expected write-lock block.";
    return false;
  }

  const auto guarded = service.GuardPayload("secret=token=api_key=abcd", false, true);
  if (!guarded.accepted || !guarded.redacted || !guarded.repaired) {
    outError = L"Fixture validation failed: expected redact and repair behavior.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
