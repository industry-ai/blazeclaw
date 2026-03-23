#pragma once

#include "EmbeddingsService.h"

#include <mutex>

namespace blazeclaw::core {

class OnnxEmbeddingsService final : public EmbeddingsService {
public:
  OnnxEmbeddingsService();
  ~OnnxEmbeddingsService() override;

  void Configure(const blazeclaw::config::AppConfig& appConfig) override;

  [[nodiscard]] EmbeddingsServiceSnapshot Snapshot() const override;

  [[nodiscard]] EmbeddingResult EmbedText(
      const EmbeddingRequest& request) override;

  [[nodiscard]] EmbeddingBatchResult EmbedBatch(
      const EmbeddingBatchRequest& request) override;

  [[nodiscard]] bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const override;

private:
  struct SessionState;

  [[nodiscard]] bool EnsureInitializedLocked(
      EmbeddingError& outError) const;

  [[nodiscard]] EmbeddingResult EmbedSingleTextLocked(
      const std::wstring& text,
      bool normalize,
      const std::string& traceId) const;

  [[nodiscard]] std::vector<std::int64_t> TokenizeText(
      const std::wstring& text,
      std::size_t maxTokens,
      EmbeddingError& outError) const;

  void ResetStateLocked();

  mutable std::mutex m_mutex;
  blazeclaw::config::AppConfig m_config;
  mutable std::unique_ptr<SessionState> m_sessionState;
  mutable EmbeddingsServiceSnapshot m_snapshot;
};

} // namespace blazeclaw::core
