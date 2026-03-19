#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

struct AuthProfileRuntimeEntry {
  std::string id;
  std::string provider;
  std::string credentialRef;
  bool enabled = true;
  std::uint32_t cooldownSeconds = 0;
  std::optional<std::uint64_t> cooldownUntilMs;
  std::optional<std::uint64_t> lastUsedAtMs;
  std::optional<std::uint64_t> lastFailureAtMs;
};

struct AuthProfileSnapshot {
  std::vector<AuthProfileRuntimeEntry> entries;
  std::vector<std::string> orderedProfileIds;
};

class AgentsAuthProfileService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);
  void Initialize(const std::filesystem::path& workspaceRoot);

  [[nodiscard]] std::optional<AuthProfileRuntimeEntry> ResolveForProvider(
      const std::string& provider,
      std::uint64_t nowMs);

  void MarkSuccess(
      const std::string& profileId,
      std::uint64_t nowMs);

  void MarkFailure(
      const std::string& profileId,
      std::uint64_t nowMs);

  [[nodiscard]] AuthProfileSnapshot Snapshot(std::uint64_t nowMs) const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] std::filesystem::path PersistencePath() const;
  void LoadState();
  void SaveState() const;

  [[nodiscard]] static std::string ToNarrow(const std::wstring& value);
  [[nodiscard]] static std::string NormalizeId(const std::string& value);

  blazeclaw::config::AppConfig m_config;
  std::filesystem::path m_workspaceRoot;
  std::unordered_map<std::string, AuthProfileRuntimeEntry> m_profiles;
};

} // namespace blazeclaw::core
