#include "pch.h"
#include "AgentsAuthProfileService.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace blazeclaw::core {

namespace {

std::vector<std::string> Split(
    const std::string& value,
    const char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    const auto next = value.find(delimiter, start);
    if (next == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }

    parts.push_back(value.substr(start, next - start));
    start = next + 1;
  }

  return parts;
}

std::uint64_t ParseUInt64(const std::string& value, const std::uint64_t fallback) {
  try {
    return static_cast<std::uint64_t>(std::stoull(value));
  } catch (...) {
    return fallback;
  }
}

} // namespace

std::string AgentsAuthProfileService::ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());
  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::string AgentsAuthProfileService::NormalizeId(const std::string& value) {
  if (value.empty()) {
    return "";
  }

  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    const auto lowered = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if ((lowered >= 'a' && lowered <= 'z') ||
        (lowered >= '0' && lowered <= '9') ||
        lowered == '-' || lowered == '_' || lowered == '.') {
      normalized.push_back(lowered);
    }
  }

  return normalized;
}

void AgentsAuthProfileService::Configure(const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
  m_profiles.clear();

  for (const auto& [profileIdWide, profileConfig] : m_config.authProfiles.entries) {
    const std::string profileId = NormalizeId(ToNarrow(
        profileConfig.id.empty() ? profileIdWide : profileConfig.id));
    if (profileId.empty()) {
      continue;
    }

    m_profiles.insert_or_assign(profileId, AuthProfileRuntimeEntry{
        .id = profileId,
        .provider = NormalizeId(ToNarrow(profileConfig.provider)),
        .credentialRef = ToNarrow(profileConfig.credentialRef),
        .enabled = profileConfig.enabled,
        .cooldownSeconds = profileConfig.cooldownSeconds,
        .cooldownUntilMs = std::nullopt,
        .lastUsedAtMs = std::nullopt,
        .lastFailureAtMs = std::nullopt,
    });
  }
}

void AgentsAuthProfileService::Initialize(const std::filesystem::path& workspaceRoot) {
  m_workspaceRoot = workspaceRoot;
  LoadState();
}

std::filesystem::path AgentsAuthProfileService::PersistencePath() const {
  return m_workspaceRoot / L".blazeclaw" / L"auth-profiles.state";
}

void AgentsAuthProfileService::LoadState() {
  const auto path = PersistencePath();
  std::ifstream input(path);
  if (!input.is_open()) {
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }

    const auto parts = Split(line, '|');
    if (parts.size() < 4) {
      continue;
    }

    const std::string profileId = NormalizeId(parts[0]);
    const auto profileIt = m_profiles.find(profileId);
    if (profileIt == m_profiles.end()) {
      continue;
    }

    if (parts[1] != "-") {
      profileIt->second.cooldownUntilMs = ParseUInt64(parts[1], 0);
    }

    if (parts[2] != "-") {
      profileIt->second.lastUsedAtMs = ParseUInt64(parts[2], 0);
    }

    if (parts[3] != "-") {
      profileIt->second.lastFailureAtMs = ParseUInt64(parts[3], 0);
    }
  }
}

void AgentsAuthProfileService::SaveState() const {
  const auto path = PersistencePath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output.is_open()) {
    return;
  }

  std::vector<std::string> keys;
  keys.reserve(m_profiles.size());
  for (const auto& [key, _] : m_profiles) {
    keys.push_back(key);
  }

  std::sort(keys.begin(), keys.end());
  for (const auto& key : keys) {
    const auto it = m_profiles.find(key);
    if (it == m_profiles.end()) {
      continue;
    }

    const auto& profile = it->second;
    output << profile.id << "|"
           << (profile.cooldownUntilMs.has_value()
                   ? std::to_string(profile.cooldownUntilMs.value())
                   : std::string("-")) << "|"
           << (profile.lastUsedAtMs.has_value()
                   ? std::to_string(profile.lastUsedAtMs.value())
                   : std::string("-")) << "|"
           << (profile.lastFailureAtMs.has_value()
                   ? std::to_string(profile.lastFailureAtMs.value())
                   : std::string("-"))
           << "\n";
  }
}

std::optional<AuthProfileRuntimeEntry> AgentsAuthProfileService::ResolveForProvider(
    const std::string& provider,
    const std::uint64_t nowMs) {
  const std::string normalizedProvider = NormalizeId(provider);

  std::vector<std::string> ordered;
  ordered.reserve(m_config.authProfiles.order.size());
  for (const auto& orderedWide : m_config.authProfiles.order) {
    const auto id = NormalizeId(ToNarrow(orderedWide));
    if (!id.empty()) {
      ordered.push_back(id);
    }
  }

  for (const auto& [key, _] : m_profiles) {
    if (std::find(ordered.begin(), ordered.end(), key) == ordered.end()) {
      ordered.push_back(key);
    }
  }

  for (const auto& profileId : ordered) {
    auto it = m_profiles.find(profileId);
    if (it == m_profiles.end()) {
      continue;
    }

    auto& profile = it->second;
    if (!profile.enabled) {
      continue;
    }

    if (!normalizedProvider.empty() && profile.provider != normalizedProvider) {
      continue;
    }

    if (profile.cooldownUntilMs.has_value() && profile.cooldownUntilMs.value() > nowMs) {
      continue;
    }

    profile.lastUsedAtMs = nowMs;
    SaveState();
    return profile;
  }

  return std::nullopt;
}

void AgentsAuthProfileService::MarkSuccess(
    const std::string& profileId,
    const std::uint64_t nowMs) {
  const auto id = NormalizeId(profileId);
  const auto it = m_profiles.find(id);
  if (it == m_profiles.end()) {
    return;
  }

  it->second.lastUsedAtMs = nowMs;
  it->second.cooldownUntilMs = std::nullopt;
  SaveState();
}

void AgentsAuthProfileService::MarkFailure(
    const std::string& profileId,
    const std::uint64_t nowMs) {
  const auto id = NormalizeId(profileId);
  const auto it = m_profiles.find(id);
  if (it == m_profiles.end()) {
    return;
  }

  it->second.lastFailureAtMs = nowMs;
  if (it->second.cooldownSeconds > 0) {
    it->second.cooldownUntilMs = nowMs +
        static_cast<std::uint64_t>(it->second.cooldownSeconds) * 1000ULL;
  }

  SaveState();
}

AuthProfileSnapshot AgentsAuthProfileService::Snapshot(const std::uint64_t nowMs) const {
  AuthProfileSnapshot snapshot;

  for (const auto& orderedWide : m_config.authProfiles.order) {
    const auto id = NormalizeId(ToNarrow(orderedWide));
    if (!id.empty()) {
      snapshot.orderedProfileIds.push_back(id);
    }
  }

  for (const auto& [id, profile] : m_profiles) {
    snapshot.entries.push_back(profile);
    if (std::find(snapshot.orderedProfileIds.begin(), snapshot.orderedProfileIds.end(), id) ==
        snapshot.orderedProfileIds.end()) {
      snapshot.orderedProfileIds.push_back(id);
    }
  }

  std::sort(
      snapshot.entries.begin(),
      snapshot.entries.end(),
      [](const AuthProfileRuntimeEntry& left, const AuthProfileRuntimeEntry& right) {
        return left.id < right.id;
      });

  for (auto& entry : snapshot.entries) {
    if (entry.cooldownUntilMs.has_value() && entry.cooldownUntilMs.value() <= nowMs) {
      entry.cooldownUntilMs = std::nullopt;
    }
  }

  return snapshot;
}

bool AgentsAuthProfileService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;

  blazeclaw::config::AuthProfileEntryConfig p1;
  p1.id = L"primary";
  p1.provider = L"seed";
  p1.credentialRef = L"env:PRIMARY_KEY";
  p1.cooldownSeconds = 1;
  p1.enabled = true;
  config.authProfiles.entries.insert_or_assign(p1.id, p1);

  blazeclaw::config::AuthProfileEntryConfig p2;
  p2.id = L"backup";
  p2.provider = L"seed";
  p2.credentialRef = L"env:BACKUP_KEY";
  p2.enabled = true;
  config.authProfiles.entries.insert_or_assign(p2.id, p2);
  config.authProfiles.order = {L"primary", L"backup"};

  AgentsAuthProfileService service;
  service.Configure(config);
  service.Initialize(fixturesRoot / L"a6-model-auth");

  const auto first = service.ResolveForProvider("seed", 1735690000000);
  if (!first.has_value() || first->id != "primary") {
    outError = L"Fixture validation failed: expected primary auth profile selection.";
    return false;
  }

  service.MarkFailure("primary", 1735690000000);

  const auto second = service.ResolveForProvider("seed", 1735690000100);
  if (!second.has_value() || second->id != "backup") {
    outError = L"Fixture validation failed: expected backup profile during cooldown.";
    return false;
  }

  service.MarkSuccess("backup", 1735690000200);
  const auto snapshot = service.Snapshot(1735690000200);
  if (snapshot.entries.empty()) {
    outError = L"Fixture validation failed: expected auth profile snapshot entries.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
