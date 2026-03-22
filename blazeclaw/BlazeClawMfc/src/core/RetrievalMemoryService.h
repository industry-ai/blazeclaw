#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>

namespace blazeclaw::core {

struct RetrievalMemoryRecord {
  std::string id;
  std::string sessionId;
  std::string role;
  std::string text;
  std::vector<float> embedding;
  std::uint64_t timestampMs = 0;
};

struct RetrievalQueryMatch {
  std::string id;
  std::string text;
  float score = 0.0f;
};

struct RetrievalMemorySnapshot {
  bool enabled = false;
  std::size_t recordCount = 0;
  std::size_t lastQueryCount = 0;
  std::string status;
};

class RetrievalMemoryService {
public:
  void Configure(const blazeclaw::config::AppConfig& appConfig);

  void Upsert(
      const std::string& sessionId,
      const std::string& role,
      const std::string& text,
      const std::vector<float>& embedding,
      std::uint64_t timestampMs);

  [[nodiscard]] std::vector<RetrievalQueryMatch> Query(
      const std::string& sessionId,
      const std::vector<float>& embedding,
      std::size_t limit) const;

  [[nodiscard]] RetrievalMemorySnapshot Snapshot() const;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const;

private:
  [[nodiscard]] static float CosineSimilarity(
      const std::vector<float>& left,
      const std::vector<float>& right);

  blazeclaw::config::AppConfig m_config;
  std::vector<RetrievalMemoryRecord> m_records;
  mutable std::size_t m_lastQueryCount = 0;
};

} // namespace blazeclaw::core
