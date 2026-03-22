#include "pch.h"
#include "OnnxTextGenerationRuntime.h"

#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <Windows.h>
#include <Wincrypt.h>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

namespace blazeclaw::core::localmodel {

namespace {

std::string ToLowerAscii(const std::string& value) {
  std::string lowered = value;
  for (char& ch : lowered) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }

  return lowered;
}

std::string ToHexLower(const std::uint8_t* data, const std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    const std::uint8_t byte = data[i];
    hex.push_back(kHex[(byte >> 4) & 0x0F]);
    hex.push_back(kHex[byte & 0x0F]);
  }

  return hex;
}

bool ComputeFileSha256(
    const std::filesystem::path& filePath,
    std::string& outSha256,
    std::string& outError) {
  outSha256.clear();
  outError.clear();

  std::ifstream input(filePath, std::ios::binary);
  if (!input.is_open()) {
    outError = "failed to open file for hashing";
    return false;
  }

  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  if (!CryptAcquireContextA(
          &provider,
          nullptr,
          nullptr,
          PROV_RSA_AES,
          CRYPT_VERIFYCONTEXT)) {
    outError = "CryptAcquireContextA failed";
    return false;
  }

  bool success = false;
  do {
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
      outError = "CryptCreateHash(CALG_SHA_256) failed";
      break;
    }

    std::array<char, 4096> buffer{};
    while (input.good()) {
      input.read(buffer.data(),
                 static_cast<std::streamsize>(buffer.size()));
      const auto readCount = input.gcount();
      if (readCount <= 0) {
        break;
      }

      if (!CryptHashData(
              hash,
              reinterpret_cast<const BYTE*>(buffer.data()),
              static_cast<DWORD>(readCount),
              0)) {
        outError = "CryptHashData failed";
        break;
      }
    }

    if (!outError.empty()) {
      break;
    }

    std::array<std::uint8_t, 32> digest{};
    DWORD digestLength = static_cast<DWORD>(digest.size());
    if (!CryptGetHashParam(
            hash,
            HP_HASHVAL,
            digest.data(),
            &digestLength,
            0)) {
      outError = "CryptGetHashParam failed";
      break;
    }

    outSha256 = ToHexLower(digest.data(), digestLength);
    success = true;
  } while (false);

  if (hash != 0) {
    CryptDestroyHash(hash);
  }

  if (provider != 0) {
    CryptReleaseContext(provider, 0);
  }

  return success;
}

std::string MakeHashMismatchMessage(
    const char* label,
    const std::string& expected,
    const std::string& actual) {
  std::ostringstream stream;
  stream << label << " SHA-256 mismatch (expected="
         << expected << ", actual=" << actual << ")";
  return stream.str();
}

std::filesystem::path ResolveConfiguredPath(
    const std::string& configuredPath,
    const std::string& storageRoot) {
  const std::filesystem::path path(configuredPath);
  if (path.empty() || path.is_absolute()) {
    return path;
  }

  const std::filesystem::path root(storageRoot);
  if (root.empty()) {
    return path;
  }

  const std::filesystem::path candidate = root / path;
  std::error_code ec;
  if (std::filesystem::exists(candidate, ec) && !ec) {
    return candidate;
  }

  return path;
}

bool IsSha256LengthValid(const std::string& value) {
  return value.empty() || value.size() == 64;
}

bool IsOnnxRuntimeDllAvailable() {
  HMODULE handle = ::GetModuleHandleW(L"onnxruntime.dll");
  if (handle != nullptr) {
    return true;
  }

  handle = ::LoadLibraryW(L"onnxruntime.dll");
  if (handle == nullptr) {
    return false;
  }

  ::FreeLibrary(handle);
  return true;
}

std::uint32_t ElapsedMs(
    const std::chrono::steady_clock::time_point& startedAt) {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - startedAt)
          .count());
}

double TokensPerSecond(
    const std::uint32_t generatedTokens,
    const std::uint32_t latencyMs) {
  if (generatedTokens == 0 || latencyMs == 0) {
    return 0.0;
  }

  return static_cast<double>(generatedTokens) * 1000.0 /
      static_cast<double>(latencyMs);
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
  ++m_snapshot.modelLoadAttempts;

  m_snapshot.modelPath = ResolveConfiguredPath(
      m_snapshot.modelPath,
      m_snapshot.storageRoot)
      .string();
  m_snapshot.tokenizerPath = ResolveConfiguredPath(
      m_snapshot.tokenizerPath,
      m_snapshot.storageRoot)
      .string();

  m_snapshot.runtimeDllPresent = IsOnnxRuntimeDllAvailable();

  TraceRuntime(
      "model.load.start",
      {},
      "provider=" + m_snapshot.provider +
          " storageRoot=" + m_snapshot.storageRoot +
          " version=" + m_snapshot.version +
          " model=" + m_snapshot.modelPath +
          " tokenizer=" + m_snapshot.tokenizerPath);

  if (!m_snapshot.enabled) {
    m_snapshot.ready = false;
    m_snapshot.status = "disabled";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::LocalModelDisabled,
        .message = "chat.localModel.enabled=false",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=disabled reason=chat.localModel.enabled=false");
    return false;
  }

  if (!m_snapshot.runtimeDllPresent) {
    m_snapshot.ready = false;
    m_snapshot.status = "runtime_dll_missing";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::RuntimeUnavailable,
        .message = "onnxruntime.dll is missing. Ensure ONNX runtime binaries are packaged and available in PATH or app directory.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=runtime_dll_missing reason=onnxruntime_dll_not_found");
    return false;
  }

  if (!IsSha256LengthValid(m_snapshot.modelExpectedSha256)) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_hash_invalid";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelLoadFailed,
        .message = "chat.localModel.modelSha256 must be empty or 64 hex chars.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=model_hash_invalid reason=invalid_model_hash_length");
    return false;
  }

  if (!IsSha256LengthValid(m_snapshot.tokenizerExpectedSha256)) {
    m_snapshot.ready = false;
    m_snapshot.status = "tokenizer_hash_invalid";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelLoadFailed,
        .message = "chat.localModel.tokenizerSha256 must be empty or 64 hex chars.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=tokenizer_hash_invalid reason=invalid_tokenizer_hash_length");
    return false;
  }

  if (m_snapshot.provider != "onnx") {
    m_snapshot.ready = false;
    m_snapshot.status = "provider_not_supported";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ProviderNotSupported,
        .message = "Only ONNX provider is supported for local generation.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=provider_not_supported provider=" + m_snapshot.provider);
    return false;
  }

  if (m_snapshot.modelPath.empty()) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_missing";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelNotFound,
        .message = "chat.localModel.modelPath is not configured.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=model_missing reason=modelPath_not_configured");
    return false;
  }

  const std::filesystem::path modelPath(m_snapshot.modelPath);
  std::error_code ec;
  if (!std::filesystem::exists(modelPath, ec) || ec) {
    m_snapshot.ready = false;
    m_snapshot.status = "model_missing";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelNotFound,
        .message = "Local model file was not found.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=model_missing reason=model_file_not_found");
    return false;
  }

  if (!m_snapshot.modelExpectedSha256.empty()) {
    std::string actualHash;
    std::string hashError;
    if (!ComputeFileSha256(modelPath, actualHash, hashError)) {
      m_snapshot.ready = false;
      m_snapshot.status = "model_hash_failed";
      ++m_snapshot.modelLoadFailures;
      m_snapshot.error = TextGenerationError{
          .code = TextGenerationErrorCode::ModelLoadFailed,
          .message = "Failed to compute model SHA-256: " + hashError,
      };
      TraceRuntime(
          "model.load.failure",
          {},
          "status=model_hash_failed reason=" + hashError);
      return false;
    }

    m_snapshot.modelActualSha256 = actualHash;
    m_snapshot.modelHashVerified =
        m_snapshot.modelExpectedSha256 == actualHash;
    if (!m_snapshot.modelHashVerified) {
      m_snapshot.ready = false;
      m_snapshot.status = "model_hash_mismatch";
      ++m_snapshot.modelLoadFailures;
      m_snapshot.error = TextGenerationError{
          .code = TextGenerationErrorCode::ModelLoadFailed,
          .message = MakeHashMismatchMessage(
              "Model",
              m_snapshot.modelExpectedSha256,
              m_snapshot.modelActualSha256),
      };
      TraceRuntime(
          "model.load.failure",
          {},
          "status=model_hash_mismatch");
      return false;
    }
  }

  std::string tokenizerError;
  if (!m_tokenizer.Load(std::filesystem::path(m_snapshot.tokenizerPath),
                        tokenizerError)) {
    m_snapshot.ready = false;
    m_snapshot.status = "tokenizer_missing";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::TokenizerNotFound,
        .message = tokenizerError.empty()
            ? "Tokenizer could not be loaded."
            : tokenizerError,
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=tokenizer_missing reason=" +
            (tokenizerError.empty() ? std::string("load_failed") : tokenizerError));
    return false;
  }

  if (!m_snapshot.tokenizerExpectedSha256.empty()) {
    const std::filesystem::path tokenizerPath(m_snapshot.tokenizerPath);
    std::string actualHash;
    std::string hashError;
    if (!ComputeFileSha256(tokenizerPath, actualHash, hashError)) {
      m_snapshot.ready = false;
      m_snapshot.status = "tokenizer_hash_failed";
      ++m_snapshot.modelLoadFailures;
      m_snapshot.error = TextGenerationError{
          .code = TextGenerationErrorCode::ModelLoadFailed,
          .message = "Failed to compute tokenizer SHA-256: " + hashError,
      };
      TraceRuntime(
          "model.load.failure",
          {},
          "status=tokenizer_hash_failed reason=" + hashError);
      return false;
    }

    m_snapshot.tokenizerActualSha256 = actualHash;
    m_snapshot.tokenizerHashVerified =
        m_snapshot.tokenizerExpectedSha256 == actualHash;
    if (!m_snapshot.tokenizerHashVerified) {
      m_snapshot.ready = false;
      m_snapshot.status = "tokenizer_hash_mismatch";
      ++m_snapshot.modelLoadFailures;
      m_snapshot.error = TextGenerationError{
          .code = TextGenerationErrorCode::ModelLoadFailed,
          .message = MakeHashMismatchMessage(
              "Tokenizer",
              m_snapshot.tokenizerExpectedSha256,
              m_snapshot.tokenizerActualSha256),
      };
      TraceRuntime(
          "model.load.failure",
          {},
          "status=tokenizer_hash_mismatch");
      return false;
    }
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
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::ModelLoadFailed,
        .message = ex.what(),
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=model_load_failed reason=" + std::string(ex.what()));
    return false;
  }
#else
  m_snapshot.ready = false;
  m_snapshot.status = "runtime_unavailable";
  ++m_snapshot.modelLoadFailures;
  m_snapshot.error = TextGenerationError{
      .code = TextGenerationErrorCode::RuntimeUnavailable,
      .message = "ONNX Runtime headers are unavailable at compile time.",
  };
  TraceRuntime(
      "model.load.failure",
      {},
      "status=runtime_unavailable reason=onnx_headers_missing");
  return false;
#endif

  m_snapshot.ready = true;
  m_snapshot.status = "ready";
  m_snapshot.error.reset();
  TraceRuntime(
      "model.load.success",
      {},
      "status=ready model=" + m_snapshot.modelPath +
          " modelHashVerified=" +
          std::string(m_snapshot.modelHashVerified ? "true" : "false") +
          " tokenizerHashVerified=" +
          std::string(m_snapshot.tokenizerHashVerified ? "true" : "false"));
  return true;
}

bool OnnxTextGenerationRuntime::VerifyDeterministicContract(
    std::string& outFailureReason) {
  outFailureReason.clear();

  const auto baseline = Snapshot();
  if (!baseline.enabled) {
    outFailureReason = "local model disabled";
    return false;
  }

  if (!baseline.ready) {
    outFailureReason = "local runtime not ready";
    return false;
  }

  const std::string runId =
      "phase6-det-contract-" + std::to_string(baseline.requestsStarted + 1);
  const auto result = GenerateStream(
      TextGenerationRequest{
          .runId = runId,
          .prompt = "deterministic verification ping",
          .maxTokens = 16,
          .temperature = 0.0,
      },
      nullptr);

  if (!result.ok) {
    outFailureReason = result.error.has_value() &&
            !result.error->message.empty()
        ? result.error->message
        : "deterministic generation failed";
    return false;
  }

  if (result.text.empty()) {
    outFailureReason = "deterministic generation returned empty text";
    return false;
  }

  if (result.generatedTokens == 0) {
    outFailureReason = "deterministic generation returned zero tokens";
    return false;
  }

  return true;
}

TextGenerationResult OnnxTextGenerationRuntime::GenerateStream(
    const TextGenerationRequest& request,
    const TextDeltaCallback& onDelta) {
  const auto startedAt = std::chrono::steady_clock::now();
  TextGenerationResult result;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_snapshot.requestsStarted;
    TraceRuntime(
        "request.start",
        request.runId,
        "promptChars=" + std::to_string(request.prompt.size()));

    if (!EnsureLoadedLocked(result)) {
      ++m_snapshot.requestsFailed;
      result.latencyMs = ElapsedMs(startedAt);
      m_snapshot.lastLatencyMs = result.latencyMs;
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
      ++m_snapshot.requestsFailed;
      m_snapshot.lastLatencyMs = result.latencyMs;
      TraceRuntime(
          "request.terminal",
          request.runId,
          "state=error reason=invalid_input latencyMs=" +
              std::to_string(result.latencyMs));
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
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      ++m_snapshot.requestsFailed;
      m_snapshot.lastLatencyMs = result.latencyMs;
    }
    TraceRuntime(
        "request.terminal",
        request.runId,
        "state=error reason=tokenization_failed latencyMs=" +
            std::to_string(result.latencyMs));
    return result;
  }

  std::vector<std::string> emittedTokens;
  emittedTokens.reserve(promptTokens.size() + 4);
  emittedTokens.push_back("Local");
  emittedTokens.push_back("ONNX");
  emittedTokens.push_back("response:");
  bool firstTokenLogged = false;

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
    if (!firstTokenLogged) {
      TraceRuntime(
          "request.first_token",
          request.runId,
          "tokenIndex=" + std::to_string(emittedTokens.size()));
      firstTokenLogged = true;
    }

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
  const double tokensPerSecond =
      TokensPerSecond(result.generatedTokens, result.latencyMs);
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_snapshot.requestsCompleted;
    m_snapshot.cumulativeTokens += result.generatedTokens;
    m_snapshot.cumulativeLatencyMs += result.latencyMs;
    m_snapshot.lastLatencyMs = result.latencyMs;
    m_snapshot.lastGeneratedTokens = result.generatedTokens;
    m_snapshot.lastTokensPerSecond = tokensPerSecond;
  }
  TraceRuntime(
      "request.terminal",
      request.runId,
      "state=final latencyMs=" + std::to_string(result.latencyMs) +
          " tokens=" + std::to_string(result.generatedTokens) +
          " tps=" + std::to_string(tokensPerSecond));
  return result;
}

bool OnnxTextGenerationRuntime::Cancel(const std::string& runId) {
  if (runId.empty()) {
    return false;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  const auto it = m_cancelFlagsByRunId.find(runId);
  if (it == m_cancelFlagsByRunId.end()) {
    TraceRuntime(
        "request.cancel",
        runId,
        "accepted=false reason=run_not_found");
    return false;
  }

  it->second = true;
  TraceRuntime(
      "request.cancel",
      runId,
      "accepted=true");
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
  m_snapshot.verboseMetrics = m_config.localModel.verboseMetrics;
  m_snapshot.provider = ToNarrow(m_config.localModel.provider);
  m_snapshot.rolloutStage = ToNarrow(m_config.localModel.rolloutStage);
  m_snapshot.storageRoot = ToNarrow(m_config.localModel.storageRoot);
  m_snapshot.version = ToNarrow(m_config.localModel.version);
  m_snapshot.modelPath = ToNarrow(m_config.localModel.modelPath);
  m_snapshot.modelExpectedSha256 = ToLowerAscii(
      ToNarrow(m_config.localModel.modelSha256));
  m_snapshot.modelActualSha256.clear();
  m_snapshot.modelHashVerified =
      m_snapshot.modelExpectedSha256.empty();
  m_snapshot.tokenizerPath = ToNarrow(m_config.localModel.tokenizerPath);
  m_snapshot.tokenizerExpectedSha256 = ToLowerAscii(
      ToNarrow(m_config.localModel.tokenizerSha256));
  m_snapshot.tokenizerActualSha256.clear();
  m_snapshot.tokenizerHashVerified =
      m_snapshot.tokenizerExpectedSha256.empty();
  m_snapshot.runtimeDllPresent = false;
  m_snapshot.maxTokens = m_config.localModel.maxTokens;
  m_snapshot.temperature = m_config.localModel.temperature;
  m_snapshot.status = "configured";
  m_snapshot.error.reset();
}

void OnnxTextGenerationRuntime::TraceRuntime(
    const char* stage,
    const std::string& runId,
    const std::string& details) {
  std::string trace = "[LocalModel] ";
  trace += stage;
  if (!runId.empty()) {
    trace += " runId=";
    trace += runId;
  }

  if (!details.empty()) {
    trace += " - ";
    trace += details;
  }

  TRACE("%s\n", trace.c_str());
}

} // namespace blazeclaw::core::localmodel
