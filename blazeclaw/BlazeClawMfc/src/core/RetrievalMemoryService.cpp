#include "pch.h"
#include "RetrievalMemoryService.h"

#include <algorithm>
#include <cmath>

namespace blazeclaw::core {

namespace {

std::string BuildRecordId(
    const std::string& sessionId,
    const std::string& role,
    const std::uint64_t timestampMs,
    const std::size_t sequence) {
  return sessionId + "-" + role + "-" +
      std::to_string(timestampMs) + "-" +
      std::to_string(sequence);
}

} // namespace

void RetrievalMemoryService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  m_config = appConfig;
  m_records.clear();
  m_lastQueryCount = 0;
}

void RetrievalMemoryService::Upsert(
    const std::string& sessionId,
    const std::string& role,
    const std::string& text,
    const std::vector<float>& embedding,
    const std::uint64_t timestampMs) {
  if (!m_config.embeddings.enabled ||
      sessionId.empty() ||
      text.empty() ||
      embedding.empty()) {
    return;
  }

  RetrievalMemoryRecord record;
  record.id = BuildRecordId(
      sessionId,
      role.empty() ? "unknown" : role,
      timestampMs,
      m_records.size());
  record.sessionId = sessionId;
  record.role = role.empty() ? "unknown" : role;
  record.text = text;
  record.embedding = embedding;
  record.timestampMs = timestampMs;

  m_records.push_back(std::move(record));
  if (m_records.size() > 512) {
    m_records.erase(m_records.begin());
  }
}

float RetrievalMemoryService::CosineSimilarity(
    const std::vector<float>& left,
    const std::vector<float>& right) {
  if (left.empty() || right.empty() ||
      left.size() != right.size()) {
    return -1.0f;
  }

  double dot = 0.0;
  double leftNorm = 0.0;
  double rightNorm = 0.0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const double leftValue = static_cast<double>(left[i]);
    const double rightValue = static_cast<double>(right[i]);
    dot += leftValue * rightValue;
    leftNorm += leftValue * leftValue;
    rightNorm += rightValue * rightValue;
  }

  if (leftNorm <= 0.0 || rightNorm <= 0.0) {
    return -1.0f;
  }

  const double denom = std::sqrt(leftNorm) * std::sqrt(rightNorm);
  if (denom <= 0.0) {
    return -1.0f;
  }

  return static_cast<float>(dot / denom);
}

std::vector<RetrievalQueryMatch> RetrievalMemoryService::Query(
    const std::string& sessionId,
    const std::vector<float>& embedding,
    const std::size_t limit) const {
  m_lastQueryCount = 0;
  if (!m_config.embeddings.enabled ||
      sessionId.empty() ||
      embedding.empty() ||
      limit == 0) {
    return {};
  }

  std::vector<RetrievalQueryMatch> matches;
  matches.reserve(m_records.size());
  for (const auto& record : m_records) {
    if (record.sessionId != sessionId) {
      continue;
    }

    const float score = CosineSimilarity(
        embedding,
        record.embedding);
    if (score < 0.0f) {
      continue;
    }

    matches.push_back(
        RetrievalQueryMatch{
            .id = record.id,
            .text = record.text,
            .score = score,
        });
  }

  std::sort(
      matches.begin(),
      matches.end(),
      [](const RetrievalQueryMatch& left, const RetrievalQueryMatch& right) {
        return left.score > right.score;
      });

  if (matches.size() > limit) {
    matches.resize(limit);
  }

  m_lastQueryCount = matches.size();
  return matches;
}

RetrievalMemorySnapshot RetrievalMemoryService::Snapshot() const {
  return RetrievalMemorySnapshot{
      .enabled = m_config.embeddings.enabled,
      .recordCount = m_records.size(),
      .lastQueryCount = m_lastQueryCount,
      .status = m_config.embeddings.enabled
          ? "ready"
          : "disabled",
  };
}

bool RetrievalMemoryService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig config;
  config.embeddings.enabled = true;

  RetrievalMemoryService service;
  service.Configure(config);

  const std::vector<float> baseline = {0.9f, 0.1f, 0.2f};
  service.Upsert("main", "user", "hello", baseline, 1735690001000);
  const auto matches = service.Query("main", baseline, 5);
  if (matches.empty() || matches.front().text != "hello") {
    outError =
        L"Fixture validation failed: retrieval query did not return expected record.";
    return false;
  }

  const auto empty = service.Query("other", baseline, 5);
  if (!empty.empty()) {
    outError =
        L"Fixture validation failed: retrieval query unexpectedly returned cross-session records.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
