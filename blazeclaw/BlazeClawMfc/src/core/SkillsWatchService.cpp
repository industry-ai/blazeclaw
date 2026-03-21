#include "pch.h"
#include "SkillsWatchService.h"

#include <chrono>

namespace blazeclaw::core {

namespace {

std::uint64_t NowMs() {
  const auto now = std::chrono::system_clock::now();
  const auto epoch = now.time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count());
}

} // namespace

std::uint64_t SkillsWatchService::ComputeFingerprint(
    const SkillsCatalogSnapshot& catalog) {
  std::uint64_t fingerprint = 1469598103934665603ull;
  constexpr std::uint64_t kPrime = 1099511628211ull;

  for (const auto& entry : catalog.entries) {
    const auto mixString = [&fingerprint](const std::wstring& value) {
      for (const wchar_t ch : value) {
        fingerprint ^= static_cast<std::uint64_t>(ch);
        fingerprint *= kPrime;
      }
    };

    mixString(entry.skillName);
    mixString(entry.skillFile.wstring());
    fingerprint ^= static_cast<std::uint64_t>(entry.precedence);
    fingerprint *= kPrime;
  }

  return fingerprint;
}

SkillsWatchSnapshot SkillsWatchService::Observe(
    const SkillsCatalogSnapshot& catalog,
    const blazeclaw::config::AppConfig& appConfig,
    const bool forceRefresh,
    const std::wstring& reason) {
  SkillsWatchSnapshot snapshot;
  snapshot.watchEnabled = appConfig.skills.load.watch;
  snapshot.debounceMs = appConfig.skills.load.watchDebounceMs;
  snapshot.timestampMs = NowMs();
  snapshot.reason = reason;

  const std::uint64_t fingerprint = ComputeFingerprint(catalog);
  const bool fingerprintChanged = !m_initialized || fingerprint != m_lastFingerprint;

  bool shouldUpdate = forceRefresh || fingerprintChanged;
  if (!forceRefresh && snapshot.watchEnabled && m_initialized && shouldUpdate) {
    if (snapshot.timestampMs >= m_lastUpdateMs &&
        snapshot.timestampMs - m_lastUpdateMs < snapshot.debounceMs) {
      shouldUpdate = false;
    }
  }

  if (!m_initialized) {
    m_initialized = true;
    shouldUpdate = true;
  }

  if (shouldUpdate) {
    if (m_version == 0) {
      m_version = 1;
    } else {
      ++m_version;
    }

    m_lastFingerprint = fingerprint;
    m_lastUpdateMs = snapshot.timestampMs;
  }

  snapshot.version = m_version;
  snapshot.fingerprint = m_lastFingerprint;
  snapshot.changed = shouldUpdate;
  return snapshot;
}

bool SkillsWatchService::ValidateFixtureScenarios(
    const std::filesystem::path& fixturesRoot,
    std::wstring& outError) {
  outError.clear();

  const SkillsCatalogService catalogService;
  blazeclaw::config::AppConfig appConfig;
  appConfig.skills.load.watch = true;
  appConfig.skills.load.watchDebounceMs = 0;

  const auto rootA = fixturesRoot / L"s3-watch" / L"workspace-a";
  const auto rootB = fixturesRoot / L"s3-watch" / L"workspace-b";

  const auto catalogA = catalogService.LoadCatalog(rootA, appConfig);
  const auto first = Observe(catalogA, appConfig, false, L"initial");
  if (!first.changed || first.version == 0) {
    outError = L"S3 watch fixture failed: initial observe should change and set version.";
    return false;
  }

  const auto second = Observe(catalogA, appConfig, false, L"same-catalog");
  if (second.changed || second.version != first.version) {
    outError = L"S3 watch fixture failed: unchanged catalog should keep version stable.";
    return false;
  }

  const auto catalogB = catalogService.LoadCatalog(rootB, appConfig);
  const auto third = Observe(catalogB, appConfig, false, L"catalog-change");
  if (!third.changed || third.version <= second.version) {
    outError = L"S3 watch fixture failed: changed catalog should bump version.";
    return false;
  }

  const auto fourth = Observe(catalogB, appConfig, true, L"manual-refresh");
  if (!fourth.changed || fourth.version <= third.version) {
    outError = L"S3 watch fixture failed: manual refresh should bump version.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
