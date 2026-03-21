#include "pch.h"
#include "AgentsModelRoutingService.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core {

namespace {

std::string ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());
  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

std::string NormalizeModelId(const std::string& value) {
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

void AddUnique(
    std::vector<std::string>& target,
    const std::string& value) {
  if (value.empty()) {
    return;
  }

  if (std::find(target.begin(), target.end(), value) != target.end()) {
    return;
  }

  target.push_back(value);
}

} // namespace

void AgentsModelRoutingService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
  m_failoverAttempts = 0;
  m_failoverHistory.clear();
}

std::string AgentsModelRoutingService::ResolveAlias(
    const std::string& modelId) const {
  const std::string normalized = NormalizeModelId(modelId);
  if (normalized.empty()) {
    return "";
  }

  for (const auto& [aliasKeyWide, aliasTargetWide] : m_config.models.aliases) {
    const std::string aliasKey = NormalizeModelId(ToNarrow(aliasKeyWide));
    if (aliasKey != normalized) {
      continue;
    }

    return NormalizeModelId(ToNarrow(aliasTargetWide));
  }

  return normalized;
}

bool AgentsModelRoutingService::IsAllowedModel(const std::string& modelId) const {
  const std::string normalized = NormalizeModelId(modelId);
  if (normalized.empty()) {
    return false;
  }

  if (m_config.models.allow.empty()) {
    return true;
  }

  for (const auto& allowedWide : m_config.models.allow) {
    const auto allowed = NormalizeModelId(ToNarrow(allowedWide));
    if (allowed == normalized) {
      return true;
    }
  }

  return false;
}

ModelSelectionResult AgentsModelRoutingService::SelectModel(
    const std::string& requestedModel,
    const std::string& routeReason) {
  const std::string configuredPrimary = NormalizeModelId(ToNarrow(m_config.models.primary));
  const std::string configuredFallback = NormalizeModelId(ToNarrow(m_config.models.fallback));

  std::string candidate = ResolveAlias(requestedModel);
  if (candidate.empty()) {
    candidate = configuredPrimary.empty() ? "default" : configuredPrimary;
  }

  if (!IsAllowedModel(candidate)) {
    candidate = configuredPrimary.empty() ? "default" : configuredPrimary;
  }

  if (!IsAllowedModel(candidate)) {
    candidate = configuredFallback.empty() ? "reasoner" : configuredFallback;
  }

  ModelSelectionResult result{
      .selectedModel = candidate.empty() ? "default" : candidate,
      .routeReason = routeReason.empty() ? "default_route" : routeReason,
      .failoverAttempt = m_failoverAttempts,
  };

  return result;
}

void AgentsModelRoutingService::RecordFailover(
    const std::string& modelId,
    const std::string& reason,
    const std::uint64_t timestampMs) {
  const std::size_t maxAttempts =
      static_cast<std::size_t>(m_config.models.maxFailoverAttempts == 0
                                   ? 1
                                   : m_config.models.maxFailoverAttempts);

  if (m_failoverAttempts < maxAttempts) {
    ++m_failoverAttempts;
  }

  m_failoverHistory.push_back(ModelFailoverRecord{
      .modelId = NormalizeModelId(modelId),
      .reason = reason,
      .timestampMs = timestampMs,
  });

  if (m_failoverHistory.size() > 64) {
    m_failoverHistory.erase(m_failoverHistory.begin());
  }
}

ModelRoutingSnapshot AgentsModelRoutingService::Snapshot() const {
  ModelRoutingSnapshot snapshot;
  snapshot.primaryModel = NormalizeModelId(ToNarrow(m_config.models.primary));
  snapshot.fallbackModel = NormalizeModelId(ToNarrow(m_config.models.fallback));

  for (const auto& value : m_config.models.allow) {
    AddUnique(snapshot.allowedModels, NormalizeModelId(ToNarrow(value)));
  }

  for (const auto& [aliasKeyWide, aliasTargetWide] : m_config.models.aliases) {
    const auto aliasKey = NormalizeModelId(ToNarrow(aliasKeyWide));
    const auto aliasTarget = NormalizeModelId(ToNarrow(aliasTargetWide));
    if (!aliasKey.empty() && !aliasTarget.empty()) {
      snapshot.aliases.insert_or_assign(aliasKey, aliasTarget);
    }
  }

  snapshot.failoverHistory = m_failoverHistory;
  return snapshot;
}

bool AgentsModelRoutingService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;
  config.models.primary = L"model-a";
  config.models.fallback = L"model-b";
  config.models.allow = {L"model-a", L"model-b"};
  config.models.aliases.insert_or_assign(L"latest", L"model-a");
  config.models.maxFailoverAttempts = 2;

  AgentsModelRoutingService service;
  service.Configure(config);

  const auto aliased = service.SelectModel("latest", "alias_test");
  if (aliased.selectedModel != "model-a") {
    outError = L"Fixture validation failed: alias resolution mismatch.";
    return false;
  }

  service.RecordFailover("model-a", "timeout", 1735689900000);
  service.RecordFailover("model-b", "rate_limit", 1735689900010);

  const auto snapshot = service.Snapshot();
  if (snapshot.failoverHistory.size() != 2) {
    outError = L"Fixture validation failed: expected failover history entries.";
    return false;
  }

  const auto fallback = service.SelectModel("not-allowed", "allowlist");
  if (fallback.selectedModel != "model-a") {
    outError = L"Fixture validation failed: expected primary fallback selection.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
