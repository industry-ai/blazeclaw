#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

struct TranscriptSafetyResult {
  bool accepted = true;
  bool repaired = false;
  bool redacted = false;
  bool writeLocked = false;
  std::string payload;
  std::vector<std::string> warnings;
};

class AgentsTranscriptSafetyService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);

  [[nodiscard]] TranscriptSafetyResult GuardPayload(
      const std::string& payload,
      bool hasWriteLock,
      bool toolResultPayload) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static std::string TruncateUtf8(
      const std::string& value,
      std::size_t maxChars);

  [[nodiscard]] static std::string RedactSecrets(const std::string& value);

  blazeclaw::config::AppConfig m_config;
};

} // namespace blazeclaw::core
