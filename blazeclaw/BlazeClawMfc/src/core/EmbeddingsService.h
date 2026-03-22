#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>

namespace blazeclaw::core {

enum class EmbeddingErrorCode {
  None,
  EmbeddingDisabled,
  ProviderNotSupported,
  ModelNotFound,
  ModelLoadFailed,
  TokenizerNotFound,
  TokenizationFailed,
  InvalidInput,
  InputTooLarge,
  ShapeMismatch,
  InferenceFailed,
  RuntimeUnavailable,
  Timeout,
};

struct EmbeddingError {
  EmbeddingErrorCode code = EmbeddingErrorCode::None;
  std::string message;
};

struct EmbeddingRequest {
  std::wstring text;
  std::optional<bool> normalize;
  std::string traceId;
};

struct EmbeddingBatchRequest {
  std::vector<std::wstring> texts;
  std::optional<bool> normalize;
  std::string traceId;
};

struct EmbeddingResult {
  bool ok = false;
  std::vector<float> vector;
  std::size_t dimension = 0;
  std::string provider;
  std::string modelId;
  std::uint32_t latencyMs = 0;
  std::optional<EmbeddingError> error;
};

struct EmbeddingBatchResult {
  bool ok = false;
  std::vector<std::vector<float>> vectors;
  std::size_t dimension = 0;
  std::string provider;
  std::string modelId;
  std::uint32_t latencyMs = 0;
  std::optional<EmbeddingError> error;
};

struct EmbeddingsServiceSnapshot {
  bool enabled = false;
  bool ready = false;
  std::string provider;
  std::string modelPath;
  std::string tokenizerPath;
  std::size_t dimension = 0;
  std::size_t maxSequenceLength = 0;
  std::string status;
  std::optional<EmbeddingError> error;
};

class EmbeddingsService {
public:
  virtual ~EmbeddingsService() = default;

  virtual void Configure(const blazeclaw::config::AppConfig& appConfig) = 0;

  [[nodiscard]] virtual EmbeddingsServiceSnapshot Snapshot() const = 0;

  [[nodiscard]] virtual EmbeddingResult EmbedText(
      const EmbeddingRequest& request) = 0;

  [[nodiscard]] virtual EmbeddingBatchResult EmbedBatch(
      const EmbeddingBatchRequest& request) = 0;

  [[nodiscard]] virtual bool ValidateFixtureScenarios(
      const std::filesystem::path& fixturesRoot,
      std::wstring& outError) const = 0;
};

[[nodiscard]] std::string EmbeddingErrorCodeToString(
    EmbeddingErrorCode code);

} // namespace blazeclaw::core
