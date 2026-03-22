#include "pch.h"
#include "OnnxEmbeddingsService.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <numeric>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

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

std::string LowercaseAscii(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (const char ch : value) {
    normalized.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch))));
  }

  return normalized;
}

std::string TrimAscii(const std::string& value) {
  const auto isSpace = [](const char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
  };

  const auto first = std::find_if_not(value.begin(), value.end(), isSpace);
  const auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace)
                        .base();

  if (first >= last) {
    return {};
  }

  return std::string(first, last);
}

std::string NormalizeProvider(const std::wstring& provider) {
  const std::string normalized =
      LowercaseAscii(TrimAscii(ToNarrow(provider)));
  if (normalized.empty()) {
    return "onnx";
  }

  return normalized;
}

std::string NormalizeExecutionMode(const std::wstring& mode) {
  const std::string normalized =
      LowercaseAscii(TrimAscii(ToNarrow(mode)));
  if (normalized == "parallel") {
    return normalized;
  }

  return "sequential";
}

bool IsTokenSeparator(const wchar_t ch) {
  return std::iswspace(ch) != 0 ||
      ch == L',' ||
      ch == L'.' ||
      ch == L';' ||
      ch == L':' ||
      ch == L'!' ||
      ch == L'?' ||
      ch == L'(' ||
      ch == L')' ||
      ch == L'[' ||
      ch == L']' ||
      ch == L'{' ||
      ch == L'}' ||
      ch == L'\"';
}

std::int64_t StableTokenId(const std::wstring& token) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const wchar_t ch : token) {
    hash ^= static_cast<std::uint64_t>(std::towlower(ch));
    hash *= 1099511628211ULL;
  }

  return static_cast<std::int64_t>((hash % 30000ULL) + 100ULL);
}

void NormalizeL2(std::vector<float>& values) {
  double sumSq = 0.0;
  for (const float value : values) {
    sumSq += static_cast<double>(value) *
        static_cast<double>(value);
  }

  const double norm = std::sqrt(sumSq);
  if (norm <= 0.0) {
    return;
  }

  for (float& value : values) {
    value = static_cast<float>(
        static_cast<double>(value) / norm);
  }
}

EmbeddingResult BuildErrorResult(
    const EmbeddingsServiceSnapshot& snapshot,
    const EmbeddingErrorCode code,
    const std::string& message,
    const std::uint32_t latencyMs = 0) {
  return EmbeddingResult{
      .ok = false,
      .vector = {},
      .dimension = snapshot.dimension,
      .provider = snapshot.provider,
      .modelId = snapshot.modelPath,
      .latencyMs = latencyMs,
      .error = EmbeddingError{
          .code = code,
          .message = message,
      },
  };
}

} // namespace

std::string EmbeddingErrorCodeToString(const EmbeddingErrorCode code) {
  switch (code) {
    case EmbeddingErrorCode::None:
      return "none";
    case EmbeddingErrorCode::EmbeddingDisabled:
      return "embedding_disabled";
    case EmbeddingErrorCode::ProviderNotSupported:
      return "provider_not_supported";
    case EmbeddingErrorCode::ModelNotFound:
      return "model_not_found";
    case EmbeddingErrorCode::ModelLoadFailed:
      return "model_load_failed";
    case EmbeddingErrorCode::TokenizerNotFound:
      return "tokenizer_not_found";
    case EmbeddingErrorCode::TokenizationFailed:
      return "tokenization_failed";
    case EmbeddingErrorCode::InvalidInput:
      return "invalid_input";
    case EmbeddingErrorCode::InputTooLarge:
      return "input_too_large";
    case EmbeddingErrorCode::ShapeMismatch:
      return "shape_mismatch";
    case EmbeddingErrorCode::InferenceFailed:
      return "inference_failed";
    case EmbeddingErrorCode::RuntimeUnavailable:
      return "runtime_unavailable";
    case EmbeddingErrorCode::Timeout:
      return "timeout";
    default:
      return "unknown";
  }
}

struct OnnxEmbeddingsService::SessionState {
#if BLAZECLAW_HAS_ONNXRUNTIME
  std::unique_ptr<Ort::Env> env;
  std::unique_ptr<Ort::SessionOptions> options;
  std::unique_ptr<Ort::Session> session;
#endif
  bool initialized = false;
};

OnnxEmbeddingsService::OnnxEmbeddingsService() = default;

OnnxEmbeddingsService::~OnnxEmbeddingsService() = default;

void OnnxEmbeddingsService::ResetStateLocked() {
  m_sessionState = std::make_unique<SessionState>();
  m_snapshot = EmbeddingsServiceSnapshot{};
  m_snapshot.enabled = m_config.embeddings.enabled;
  m_snapshot.provider = NormalizeProvider(
      m_config.embeddings.provider);
  m_snapshot.modelPath = ToNarrow(m_config.embeddings.modelPath);
  m_snapshot.tokenizerPath = ToNarrow(m_config.embeddings.tokenizerPath);
  m_snapshot.dimension =
      static_cast<std::size_t>(m_config.embeddings.dimension);
  m_snapshot.maxSequenceLength =
      static_cast<std::size_t>(m_config.embeddings.maxSequenceLength);
  m_snapshot.status = "configured";
}

void OnnxEmbeddingsService::Configure(
    const blazeclaw::config::AppConfig& appConfig) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_config = appConfig;
  ResetStateLocked();
}

EmbeddingsServiceSnapshot OnnxEmbeddingsService::Snapshot() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_snapshot;
}

bool OnnxEmbeddingsService::EnsureInitializedLocked(
    EmbeddingError& outError) const {
  if (!m_sessionState) {
    m_sessionState = std::make_unique<SessionState>();
  }

  if (!m_snapshot.enabled) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::EmbeddingDisabled,
        .message = "embeddings are disabled",
    };
    m_snapshot.ready = false;
    m_snapshot.status = "disabled";
    m_snapshot.error = outError;
    return false;
  }

  if (m_snapshot.provider != "onnx") {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::ProviderNotSupported,
        .message = "only onnx provider is supported",
    };
    m_snapshot.ready = false;
    m_snapshot.status = "unsupported_provider";
    m_snapshot.error = outError;
    return false;
  }

  if (m_snapshot.modelPath.empty() ||
      !std::filesystem::exists(m_config.embeddings.modelPath)) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::ModelNotFound,
        .message = "embedding model path is not available",
    };
    m_snapshot.ready = false;
    m_snapshot.status = "model_missing";
    m_snapshot.error = outError;
    return false;
  }

  if (m_snapshot.tokenizerPath.empty() ||
      !std::filesystem::exists(m_config.embeddings.tokenizerPath)) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::TokenizerNotFound,
        .message = "embedding tokenizer path is not available",
    };
    m_snapshot.ready = false;
    m_snapshot.status = "tokenizer_missing";
    m_snapshot.error = outError;
    return false;
  }

#if !BLAZECLAW_HAS_ONNXRUNTIME
  outError = EmbeddingError{
      .code = EmbeddingErrorCode::RuntimeUnavailable,
      .message = "onnxruntime headers not available at compile time",
  };
  m_snapshot.ready = false;
  m_snapshot.status = "runtime_unavailable";
  m_snapshot.error = outError;
  return false;
#else
  if (m_sessionState->initialized) {
    return true;
  }

  try {
    m_sessionState->env = std::make_unique<Ort::Env>(
        ORT_LOGGING_LEVEL_WARNING,
        "blazeclaw-embeddings");
    m_sessionState->options =
        std::make_unique<Ort::SessionOptions>();

    const int intraThreads = static_cast<int>(
        m_config.embeddings.intraThreads);
    const int interThreads = static_cast<int>(
        m_config.embeddings.interThreads);
    if (intraThreads > 0) {
      m_sessionState->options->SetIntraOpNumThreads(intraThreads);
    }

    if (interThreads > 0) {
      m_sessionState->options->SetInterOpNumThreads(interThreads);
    }

    const std::string executionMode = NormalizeExecutionMode(
        m_config.embeddings.executionMode);
    if (executionMode == "parallel") {
      m_sessionState->options->SetExecutionMode(
          ExecutionMode::ORT_PARALLEL);
    } else {
      m_sessionState->options->SetExecutionMode(
          ExecutionMode::ORT_SEQUENTIAL);
    }

    m_sessionState->session = std::make_unique<Ort::Session>(
        *m_sessionState->env,
        m_config.embeddings.modelPath.c_str(),
        *m_sessionState->options);
    m_sessionState->initialized = true;
    m_snapshot.ready = true;
    m_snapshot.status = "ready";
    m_snapshot.error = std::nullopt;
    return true;
  } catch (const Ort::Exception& ex) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::ModelLoadFailed,
        .message = ex.what(),
    };
    m_snapshot.ready = false;
    m_snapshot.status = "model_load_failed";
    m_snapshot.error = outError;
    return false;
  } catch (const std::exception& ex) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::ModelLoadFailed,
        .message = ex.what(),
    };
    m_snapshot.ready = false;
    m_snapshot.status = "model_load_failed";
    m_snapshot.error = outError;
    return false;
  }
#endif
}

std::vector<std::int64_t> OnnxEmbeddingsService::TokenizeText(
    const std::wstring& text,
    const std::size_t maxTokens,
    EmbeddingError& outError) const {
  if (maxTokens < 2) {
    outError = EmbeddingError{
        .code = EmbeddingErrorCode::InputTooLarge,
        .message = "max sequence length is too small",
    };
    return {};
  }

  std::vector<std::wstring> tokens;
  std::wstring current;
  for (const wchar_t ch : text) {
    if (IsTokenSeparator(ch)) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }

    current.push_back(ch);
  }

  if (!current.empty()) {
    tokens.push_back(current);
  }

  std::vector<std::int64_t> ids;
  ids.reserve(maxTokens);
  ids.push_back(101);
  for (const auto& token : tokens) {
    if (ids.size() + 1 >= maxTokens) {
      break;
    }

    ids.push_back(StableTokenId(token));
  }
  ids.push_back(102);

  if (ids.size() < maxTokens) {
    ids.resize(maxTokens, 0);
  }

  outError = EmbeddingError{};
  return ids;
}

EmbeddingResult OnnxEmbeddingsService::EmbedSingleTextLocked(
    const std::wstring& text,
    const bool normalize,
    const std::string& traceId) const {
  (void)traceId;

  const auto startedAt = std::chrono::steady_clock::now();

  if (text.empty()) {
    return BuildErrorResult(
        m_snapshot,
        EmbeddingErrorCode::InvalidInput,
        "text input must not be empty");
  }

  EmbeddingError initError;
  if (!EnsureInitializedLocked(initError)) {
    return BuildErrorResult(
        m_snapshot,
        initError.code,
        initError.message);
  }

  const std::size_t maxTokens =
      static_cast<std::size_t>((std::max)(
          std::uint32_t{2},
          m_config.embeddings.maxSequenceLength));

  EmbeddingError tokenError;
  auto inputIds = TokenizeText(
      text,
      maxTokens,
      tokenError);
  if (inputIds.empty()) {
    return BuildErrorResult(
        m_snapshot,
        tokenError.code == EmbeddingErrorCode::None
            ? EmbeddingErrorCode::TokenizationFailed
            : tokenError.code,
        tokenError.message.empty()
            ? "tokenization failed"
            : tokenError.message);
  }

#if !BLAZECLAW_HAS_ONNXRUNTIME
  return BuildErrorResult(
      m_snapshot,
      EmbeddingErrorCode::RuntimeUnavailable,
      "onnxruntime is unavailable");
#else
  try {
    std::vector<std::int64_t> attentionMask(maxTokens, 0);
    for (std::size_t i = 0; i < maxTokens; ++i) {
      attentionMask[i] = inputIds[i] == 0 ? 0 : 1;
    }

    std::vector<std::int64_t> tokenTypeIds(maxTokens, 0);
    std::array<std::int64_t, 2> inputShape{
        1,
        static_cast<std::int64_t>(maxTokens)};

    Ort::AllocatorWithDefaultOptions allocator;
    const auto memoryInfo = Ort::MemoryInfo::CreateCpu(
        OrtArenaAllocator,
        OrtMemTypeDefault);
    const std::size_t inputCount =
        m_sessionState->session->GetInputCount();

    std::vector<Ort::Value> inputTensors;
    std::vector<const char*> inputNames;
    std::vector<std::string> inputNameStorage;
    inputTensors.reserve(inputCount);
    inputNames.reserve(inputCount);
    inputNameStorage.reserve(inputCount);

    for (std::size_t index = 0; index < inputCount; ++index) {
      const auto nameAllocated =
          m_sessionState->session->GetInputNameAllocated(
              index,
              allocator);
      const char* inputName = nameAllocated.get();
      if (!inputName) {
        continue;
      }

      const std::string inputNameStr = LowercaseAscii(inputName);
      inputNameStorage.push_back(inputName);
      inputNames.push_back(inputNameStorage.back().c_str());

      if (inputNameStr.find("attention") != std::string::npos) {
        inputTensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
            memoryInfo,
            attentionMask.data(),
            attentionMask.size(),
            inputShape.data(),
            inputShape.size()));
        continue;
      }

      if (inputNameStr.find("token_type") != std::string::npos) {
        inputTensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
            memoryInfo,
            tokenTypeIds.data(),
            tokenTypeIds.size(),
            inputShape.data(),
            inputShape.size()));
        continue;
      }

      inputTensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
          memoryInfo,
          inputIds.data(),
          inputIds.size(),
          inputShape.data(),
          inputShape.size()));
    }

    const std::size_t outputCount =
        m_sessionState->session->GetOutputCount();
    std::vector<const char*> outputNames;
    std::vector<std::string> outputNameStorage;
    outputNames.reserve(outputCount);
    outputNameStorage.reserve(outputCount);
    for (std::size_t index = 0; index < outputCount; ++index) {
      const auto nameAllocated =
          m_sessionState->session->GetOutputNameAllocated(
              index,
              allocator);
      const char* outputName = nameAllocated.get();
      if (!outputName) {
        continue;
      }

      outputNameStorage.push_back(outputName);
      outputNames.push_back(outputNameStorage.back().c_str());
    }

    auto outputValues = m_sessionState->session->Run(
        Ort::RunOptions{nullptr},
        inputNames.data(),
        inputTensors.data(),
        inputTensors.size(),
        outputNames.data(),
        outputNames.size());

    if (outputValues.empty() || !outputValues.front().IsTensor()) {
      return BuildErrorResult(
          m_snapshot,
          EmbeddingErrorCode::InferenceFailed,
          "onnx output tensor is missing");
    }

    auto outputInfo = outputValues.front().GetTensorTypeAndShapeInfo();
    const auto outputShape = outputInfo.GetShape();
    const float* outputData = outputValues.front().GetTensorData<float>();
    if (!outputData || outputShape.empty()) {
      return BuildErrorResult(
          m_snapshot,
          EmbeddingErrorCode::ShapeMismatch,
          "onnx output shape is invalid");
    }

    std::vector<float> embedding;
    if (outputShape.size() == 2 && outputShape[0] == 1) {
      const std::size_t dimension =
          static_cast<std::size_t>((std::max)(
              std::int64_t{0},
              outputShape[1]));
      if (dimension == 0) {
        return BuildErrorResult(
            m_snapshot,
            EmbeddingErrorCode::ShapeMismatch,
            "onnx embedding dimension is zero");
      }

      embedding.assign(
          outputData,
          outputData + dimension);
    } else if (outputShape.size() == 3 && outputShape[0] == 1) {
      const std::size_t seqLength = static_cast<std::size_t>((std::max)(
          std::int64_t{0},
          outputShape[1]));
      const std::size_t hiddenSize = static_cast<std::size_t>((std::max)(
          std::int64_t{0},
          outputShape[2]));
      if (seqLength == 0 || hiddenSize == 0) {
        return BuildErrorResult(
            m_snapshot,
            EmbeddingErrorCode::ShapeMismatch,
            "onnx sequence output shape is invalid");
      }

      embedding.assign(hiddenSize, 0.0f);
      std::size_t usedTokenCount = 0;
      for (std::size_t tokenIndex = 0; tokenIndex < seqLength; ++tokenIndex) {
        if (tokenIndex >= attentionMask.size() ||
            attentionMask[tokenIndex] == 0) {
          continue;
        }

        const float* tokenRow =
            outputData + (tokenIndex * hiddenSize);
        for (std::size_t hidden = 0; hidden < hiddenSize; ++hidden) {
          embedding[hidden] += tokenRow[hidden];
        }
        ++usedTokenCount;
      }

      if (usedTokenCount == 0) {
        return BuildErrorResult(
            m_snapshot,
            EmbeddingErrorCode::ShapeMismatch,
            "onnx sequence output had no active tokens");
      }

      const float divisor = static_cast<float>(usedTokenCount);
      for (float& value : embedding) {
        value /= divisor;
      }
    } else {
      return BuildErrorResult(
          m_snapshot,
          EmbeddingErrorCode::ShapeMismatch,
          "onnx output shape is unsupported");
    }

    if (m_config.embeddings.dimension > 0 &&
        embedding.size() !=
            static_cast<std::size_t>(m_config.embeddings.dimension)) {
      return BuildErrorResult(
          m_snapshot,
          EmbeddingErrorCode::ShapeMismatch,
          "embedding dimension mismatch with config");
    }

    if (normalize) {
      NormalizeL2(embedding);
    }

    const auto endedAt = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        endedAt - startedAt);

    return EmbeddingResult{
        .ok = true,
        .vector = std::move(embedding),
        .dimension =
            static_cast<std::size_t>(m_config.embeddings.dimension),
        .provider = m_snapshot.provider,
        .modelId = m_snapshot.modelPath,
        .latencyMs = static_cast<std::uint32_t>((std::max)(
            std::int64_t{0},
            elapsed.count())),
        .error = std::nullopt,
    };
  } catch (const Ort::Exception& ex) {
    return BuildErrorResult(
        m_snapshot,
        EmbeddingErrorCode::InferenceFailed,
        ex.what());
  } catch (const std::exception& ex) {
    return BuildErrorResult(
        m_snapshot,
        EmbeddingErrorCode::InferenceFailed,
        ex.what());
  }
#endif
}

EmbeddingResult OnnxEmbeddingsService::EmbedText(
    const EmbeddingRequest& request) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const bool normalize = request.normalize.value_or(
      m_config.embeddings.normalize);
  return EmbedSingleTextLocked(
      request.text,
      normalize,
      request.traceId);
}

EmbeddingBatchResult OnnxEmbeddingsService::EmbedBatch(
    const EmbeddingBatchRequest& request) {
  const auto startedAt = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(m_mutex);
  if (request.texts.empty()) {
    return EmbeddingBatchResult{
        .ok = false,
        .vectors = {},
        .dimension = static_cast<std::size_t>(
            m_config.embeddings.dimension),
        .provider = m_snapshot.provider,
        .modelId = m_snapshot.modelPath,
        .latencyMs = 0,
        .error = EmbeddingError{
            .code = EmbeddingErrorCode::InvalidInput,
            .message = "batch must include at least one text",
        },
    };
  }

  std::vector<std::vector<float>> vectors;
  vectors.reserve(request.texts.size());
  const bool normalize = request.normalize.value_or(
      m_config.embeddings.normalize);

  for (const auto& text : request.texts) {
    auto single = EmbedSingleTextLocked(
        text,
        normalize,
        request.traceId);
    if (!single.ok) {
      const auto endedAt = std::chrono::steady_clock::now();
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          endedAt - startedAt);
      return EmbeddingBatchResult{
          .ok = false,
          .vectors = {},
          .dimension = single.dimension,
          .provider = single.provider,
          .modelId = single.modelId,
          .latencyMs = static_cast<std::uint32_t>((std::max)(
              std::int64_t{0},
              elapsed.count())),
          .error = single.error,
      };
    }

    vectors.push_back(std::move(single.vector));
  }

  const auto endedAt = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      endedAt - startedAt);

  return EmbeddingBatchResult{
      .ok = true,
      .vectors = std::move(vectors),
      .dimension = static_cast<std::size_t>(
          m_config.embeddings.dimension),
      .provider = m_snapshot.provider,
      .modelId = m_snapshot.modelPath,
      .latencyMs = static_cast<std::uint32_t>((std::max)(
          std::int64_t{0},
          elapsed.count())),
      .error = std::nullopt,
  };
}

bool OnnxEmbeddingsService::ValidateFixtureScenarios(
    const std::filesystem::path& /*fixturesRoot*/,
    std::wstring& outError) const {
  outError.clear();

  blazeclaw::config::AppConfig disabledConfig;
  disabledConfig.embeddings.enabled = false;
  disabledConfig.embeddings.provider = L"onnx";

  OnnxEmbeddingsService disabledService;
  disabledService.Configure(disabledConfig);
  const auto disabledResult = disabledService.EmbedText(
      EmbeddingRequest{
          .text = L"hello",
          .normalize = std::nullopt,
          .traceId = "fixture-disabled",
      });
  if (disabledResult.ok || !disabledResult.error.has_value() ||
      disabledResult.error->code != EmbeddingErrorCode::EmbeddingDisabled) {
    outError =
        L"Fixture validation failed: expected embeddings-disabled rejection.";
    return false;
  }

  blazeclaw::config::AppConfig missingModelConfig;
  missingModelConfig.embeddings.enabled = true;
  missingModelConfig.embeddings.provider = L"onnx";
  missingModelConfig.embeddings.modelPath = L"missing/model.onnx";
  missingModelConfig.embeddings.tokenizerPath = L"missing/tokenizer.json";

  OnnxEmbeddingsService missingModelService;
  missingModelService.Configure(missingModelConfig);
  const auto missingModelResult = missingModelService.EmbedText(
      EmbeddingRequest{
          .text = L"hello",
          .normalize = std::nullopt,
          .traceId = "fixture-model-missing",
      });
  if (missingModelResult.ok || !missingModelResult.error.has_value() ||
      missingModelResult.error->code != EmbeddingErrorCode::ModelNotFound) {
    outError =
        L"Fixture validation failed: expected model-not-found rejection.";
    return false;
  }

  blazeclaw::config::AppConfig invalidProviderConfig;
  invalidProviderConfig.embeddings.enabled = true;
  invalidProviderConfig.embeddings.provider = L"custom-provider";

  OnnxEmbeddingsService invalidProviderService;
  invalidProviderService.Configure(invalidProviderConfig);
  const auto invalidProviderResult = invalidProviderService.EmbedText(
      EmbeddingRequest{
          .text = L"hello",
          .normalize = std::nullopt,
          .traceId = "fixture-provider",
      });
  if (invalidProviderResult.ok || !invalidProviderResult.error.has_value() ||
      invalidProviderResult.error->code !=
          EmbeddingErrorCode::ProviderNotSupported) {
    outError =
        L"Fixture validation failed: expected provider-not-supported rejection.";
    return false;
  }

  return true;
}

} // namespace blazeclaw::core
