#pragma once

#include "../../../config/ConfigModels.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core::localmodel {

enum class TextGenerationErrorCode {
  None,
  LocalModelDisabled,
  ProviderNotSupported,
  ModelNotFound,
  ModelLoadFailed,
  TokenizerNotFound,
  TokenizationFailed,
  InvalidInput,
  InputTooLarge,
  InferenceFailed,
  RuntimeUnavailable,
  Cancelled,
};

struct TextGenerationError {
  TextGenerationErrorCode code = TextGenerationErrorCode::None;
  std::string message;
};

struct LocalModelRuntimeSnapshot {
  bool enabled = false;
  bool ready = false;
  bool verboseMetrics = false;
  std::string provider;
  std::string modelPath;
  std::string tokenizerPath;
  std::uint32_t maxTokens = 256;
  double temperature = 0.0;
  std::uint64_t modelLoadAttempts = 0;
  std::uint64_t modelLoadFailures = 0;
  std::uint64_t requestsStarted = 0;
  std::uint64_t requestsCompleted = 0;
  std::uint64_t requestsFailed = 0;
  std::uint64_t requestsCancelled = 0;
  std::uint64_t cumulativeTokens = 0;
  std::uint64_t cumulativeLatencyMs = 0;
  std::uint32_t lastLatencyMs = 0;
  std::uint32_t lastGeneratedTokens = 0;
  double lastTokensPerSecond = 0.0;
  std::string status;
  std::optional<TextGenerationError> error;
};

struct TextGenerationRequest {
  std::string runId;
  std::string prompt;
  std::optional<std::uint32_t> maxTokens;
  std::optional<double> temperature;
};

struct TextGenerationResult {
  bool ok = false;
  bool cancelled = false;
  std::string text;
  std::string modelId;
  std::uint32_t latencyMs = 0;
  std::uint32_t generatedTokens = 0;
  std::optional<TextGenerationError> error;
};

using TextDeltaCallback = std::function<void(const std::string& delta)>;

class ITextGenerationRuntime {
public:
  virtual ~ITextGenerationRuntime() = default;

  virtual void Configure(const blazeclaw::config::AppConfig& appConfig) = 0;

  [[nodiscard]] virtual LocalModelRuntimeSnapshot Snapshot() const = 0;

  [[nodiscard]] virtual bool LoadModel() = 0;

  [[nodiscard]] virtual TextGenerationResult GenerateStream(
      const TextGenerationRequest& request,
      const TextDeltaCallback& onDelta) = 0;

  [[nodiscard]] virtual bool Cancel(const std::string& runId) = 0;
};

[[nodiscard]] std::string TextGenerationErrorCodeToString(
    TextGenerationErrorCode code);

} // namespace blazeclaw::core::localmodel
