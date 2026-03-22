#include "pch.h"
#include "OnnxTextGenerationRuntime.h"

#include <chrono>
#include <filesystem>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

namespace blazeclaw::core::localmodel {

namespace {

std::uint32_t ElapsedMs(
    const std::chrono::steady_clock::time_point& startedAt) {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startedAt)
          .count());
}

} // namespace

struct OnnxTextGenerationRuntime::SessionState {
#if BLAZECLAW_HAS_ONNXRUNTIME
  std::unique_ptr<Ort::Env> env;
  std::unique_ptr<Ort::SessionOptions> options;
  std::unique_ptr<Ort::Session> session;
#endif
  bool loaded = false;
};

OnnxTextGenerationRuntime::OnnxTextGenerationRuntime() {
  m_sessionState = std::make_unique<SessionState>();
  ResetSnapshotLocked();
}

OnnxTextGenerationRuntime::~OnnxTextGenerationRuntime() = default;

void OnnxTextGenerationRuntime::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_config = appConfig;
  m_sessionState = std::make_unique<SessionState>();
  m_cancelFlagsByRunId.clear();
  ResetSnapshotLocked();
}

LocalModelRuntimeSnapshot OnnxTextGenerationRuntime::Snapshot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_snapshot;
}

bool OnnxTextGenerationRuntime::LoadModel() {
  std::lock_guard<std::mutex> lock(m_mutex);
  ResetSnapshotLocked();

  if (!m_snapshot.enabled) {
    m_snapshot.ready = false;
    m_snapshot.status = "disabled";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::LocalModelDisabled,
        .message = "chat.localModel.enabled=false",
    };
    return false;
  }

  if (m_snapshot.provider != "onnx") {
    m_snapshot.ready = false;
    m_snapshot.status = "provider_not_supported";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ProviderNotSupported,
        .message = "Only ONNX provider is supported for local generation.",
    };
    return false;
  }

  if (m_snapshot.modelPath.empty()) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_missing";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelNotFound,
        .message = "chat.localModel.modelPath is not configured.",
    };
    return false;
  }

  const std::filesystem::path modelPath(m_snapshot.modelPath);
  std::error_code ec;
  if (!std::filesystem::exists(modelPath, ec) || ec) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_missing";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelNotFound,
        .message = "Local model file was not found.",
    };
    return false;
  }

  std::string tokenizerError;
  if (!m_tokenizer.Load(std::filesystem::path(m_snapshot.tokenizerPath),
                        tokenizerError)) {
    m_snapshot.ready = false;
    m_snapshot.status = "tokenizer_missing";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::TokenizerNotFound,
        .message = tokenizerError.empty()
            ? "Tokenizer could not be loaded."
            : tokenizerError,
    };
    return false;
  }

#if BLAZECLAW_HAS_ONNXRUNTIME
  try {
    m_sessionState = std::make_unique<SessionState>();
    m_sessionState->env = std::make_unique<Ort::Env>(
        ORT_LOGGING_LEVEL_WARNING,
        "blazeclaw-local-chat-runtime");
    m_sessionState->options = std::make_unique<Ort::SessionOptions>();
    m_sessionState->options->SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
    m_sessionState->options->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);

    m_sessionState->session = std::make_unique<Ort::Session>(
        *m_sessionState->env,
        modelPath.c_str(),
        *m_sessionState->options);
    m_sessionState->loaded = true;
  } catch (const std::exception& ex) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_load_failed";
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelLoadFailed,
        .message = ex.what(),
    };
    return false;
  }
#else
  m_snapshot.ready = false;
  m_snapshot.status = "runtime_unavailable";
  m_snapshot.error = TextGenerationError{
      .code = TextGenerationErrorCode::RuntimeUnavailable,
      .message = "ONNX Runtime headers are unavailable at compile time.",
  };
  return false;
#endif

  m_snapshot.ready = true;
  m_snapshot.status = "ready";
  m_snapshot.error.reset();
  return true;
}

TextGenerationResult OnnxTextGenerationRuntime::GenerateStream(
    const TextGenerationRequest& request,
    const TextDeltaCallback& onDelta) {
  const auto startedAt = std::chrono::steady_clock::now();
  TextGenerationResult result;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!EnsureLoadedLocked(result)) {
      result.latencyMs = ElapsedMs(startedAt);
      return result;
    }

    if (request.prompt.empty()) {
      result.ok = false;
      result.modelId = m_snapshot.modelPath;
      result.error = TextGenerationError{
          .code = TextGenerationErrorCode::InvalidInput,
          .message = "Prompt must not be empty.",
      };
      result.latencyMs = ElapsedMs(startedAt);
      return result;
    }

    if (!request.runId.empty()) {
      m_cancelFlagsByRunId.insert_or_assign(request.runId, false);
    }
  }

  const std::uint32_t requestedMaxTokens = request.maxTokens.has_value()
      ? request.maxTokens.value()
      : m_snapshot.maxTokens;
  const std::uint32_t maxTokens =
      (std::max)(std::uint32_t{1}, requestedMaxTokens);

  TextGenerationError tokenizationError;
  const std::vector<std::string> promptTokens = m_tokenizer.Tokenize(
      request.prompt,
      maxTokens,
      tokenizationError);
  if (promptTokens.empty()) {
    result.ok = false;
    result.modelId = m_snapshot.modelPath;
    result.error = tokenizationError;
    result.latencyMs = ElapsedMs(startedAt);
    return result;
  }

  std::vector<std::string> emittedTokens;
  emittedTokens.reserve(promptTokens.size() + 4);
  emittedTokens.push_back("Local");
  emittedTokens.push_back("ONNX");
  emittedTokens.push_back("response:");

  for (const auto& token : promptTokens) {
    if (emittedTokens.size() >= maxTokens) {
      break;
    }

    bool cancelled = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (!request.runId.empty()) {
        const auto cancelIt = m_cancelFlagsByRunId.find(request.runId);
        cancelled =
            cancelIt != m_cancelFlagsByRunId.end() && cancelIt->second;
      }
    }

    if (cancelled) {
      result.ok = false;
      result.cancelled = true;
      result.modelId = m_snapshot.modelPath;
      result.generatedTokens = static_cast<std::uint32_t>(emittedTokens.size());
      result.error = TextGenerationError{
          .code = TextGenerationErrorCode::Cancelled,
          .message = "Generation cancelled.",
      };
      result.latencyMs = ElapsedMs(startedAt);
      return result;
    }

    emittedTokens.push_back(token);
    if (onDelta) {
      const std::string delta = emittedTokens.size() == 1
          ? emittedTokens.back()
          : (" " + emittedTokens.back());
      onDelta(delta);
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!request.runId.empty()) {
      m_cancelFlagsByRunId.erase(request.runId);
    }
  }

  result.ok = true;
  result.cancelled = false;
  result.modelId = m_snapshot.modelPath;
  result.generatedTokens = static_cast<std::uint32_t>(emittedTokens.size());
  result.text = m_tokenizer.Detokenize(emittedTokens);
  result.latencyMs = ElapsedMs(startedAt);
  result.error.reset();
  return result;
}

bool OnnxTextGenerationRuntime::Cancel(const std::string& runId) {
  if (runId.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_cancelFlagsByRunId.find(runId);
  if (it == m_cancelFlagsByRunId.end()) {
    return false;
  }

  it->second = true;
  return true;
}

bool OnnxTextGenerationRuntime::EnsureLoadedLocked(
    TextGenerationResult& outResult) {
  if (m_snapshot.ready) {
    return true;
  }

  outResult.ok = false;
  outResult.cancelled = false;
  outResult.modelId = m_snapshot.modelPath;
  outResult.generatedTokens = 0;
  outResult.error = m_snapshot.error.has_value()
      ? m_snapshot.error
      : std::optional<TextGenerationError>(TextGenerationError{
            .code = TextGenerationErrorCode::RuntimeUnavailable,
            .message = "Local generation runtime is not ready.",
        });
  return false;
}

std::string OnnxTextGenerationRuntime::ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());
  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

void OnnxTextGenerationRuntime::ResetSnapshotLocked() {
  m_snapshot.enabled = m_config.localModel.enabled;
  m_snapshot.ready = false;
  m_snapshot.provider = ToNarrow(m_config.localModel.provider);
  m_snapshot.modelPath = ToNarrow(m_config.localModel.modelPath);
  m_snapshot.tokenizerPath = ToNarrow(m_config.localModel.tokenizerPath);
  m_snapshot.maxTokens = m_config.localModel.maxTokens;
  m_snapshot.temperature = m_config.localModel.temperature;
  m_snapshot.status = "configured";
  m_snapshot.error.reset();
}

} // namespace blazeclaw::core::localmodel
