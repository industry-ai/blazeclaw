#include "pch.h"
#include "OnnxTextGenerationRuntime.h"

#include <chrono>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
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

#if BLAZECLAW_HAS_ONNXRUNTIME
void ConfigureDefaultSessionOptions(Ort::SessionOptions& options) {
  options.SetGraphOptimizationLevel(
      GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
  options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
}

bool TryAppendDirectMlExecutionProvider(
    Ort::SessionOptions& options,
    std::string& outReason) {
  outReason.clear();

  using AppendDirectMlFn = OrtStatus*(ORT_API_CALL*)(
      OrtSessionOptions*,
      int);

  HMODULE onnxRuntimeModule = ::GetModuleHandleW(L"onnxruntime.dll");
  if (onnxRuntimeModule == nullptr) {
    outReason = "onnxruntime.dll not loaded";
    return false;
  }

  const auto appendDirectMl = reinterpret_cast<AppendDirectMlFn>(
      ::GetProcAddress(
          onnxRuntimeModule,
          "OrtSessionOptionsAppendExecutionProvider_DML"));
  if (appendDirectMl == nullptr) {
    outReason = "DirectML execution provider API unavailable";
    return false;
  }

  OrtStatus* status = appendDirectMl(
      options,
      0);
  if (status != nullptr) {
    const OrtApi& api = Ort::GetApi();
    const char* message = api.GetErrorMessage(status);
    outReason = message == nullptr
        ? "DirectML provider append failed"
        : message;
    api.ReleaseStatus(status);
    return false;
  }

  return true;
}
#endif

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
  const std::filesystem::path configured(configuredPath);
  if (configured.empty()) {
    return configured;
  }

  if (configured.is_absolute()) {
    return configured.lexically_normal();
  }

  std::error_code ec;
  const std::filesystem::path configDirectory =
      std::filesystem::current_path(ec);
  ec.clear();

  std::filesystem::path executableDirectory;
  std::array<wchar_t, MAX_PATH> modulePath{};
  const DWORD moduleLength = ::GetModuleFileNameW(
      nullptr,
      modulePath.data(),
      static_cast<DWORD>(modulePath.size()));
  if (moduleLength > 0) {
    executableDirectory = std::filesystem::path(
        std::wstring(modulePath.data(), moduleLength))
                              .parent_path();
  }

  const std::filesystem::path root(storageRoot);
  const bool hasStorageRoot = !root.empty();

  std::vector<std::filesystem::path> candidates;
  candidates.reserve(8);

  if (hasStorageRoot) {
    if (root.is_absolute()) {
      candidates.push_back(root / configured);
    } else {
      if (!configDirectory.empty()) {
        candidates.push_back(configDirectory / root / configured);
      }

      if (!executableDirectory.empty()) {
        candidates.push_back(executableDirectory / root / configured);
      }

      candidates.push_back(root / configured);
    }
  }

  if (!configDirectory.empty()) {
    candidates.push_back(configDirectory / configured);
  }

  if (!executableDirectory.empty()) {
    candidates.push_back(executableDirectory / configured);
  }

  candidates.push_back(configured);

  for (const auto& candidate : candidates) {
    const std::filesystem::path normalized = candidate.lexically_normal();
    ec.clear();
    if (std::filesystem::exists(normalized, ec) && !ec) {
      return normalized;
    }
  }

  for (const auto& candidate : candidates) {
    if (candidate.is_absolute()) {
      return candidate.lexically_normal();
    }
  }

  return configured.lexically_normal();
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

std::vector<std::int64_t> BuildPositionIds(
    const std::size_t count) {
  std::vector<std::int64_t> ids;
  ids.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    ids.push_back(static_cast<std::int64_t>(i));
  }

  return ids;
}

std::string StripQwenThinkingAndControlTokens(
    const std::string& value) {
  std::string output = value;

  auto eraseAll = [&output](const std::string& token) {
    std::size_t pos = output.find(token);
    while (pos != std::string::npos) {
      output.erase(pos, token.size());
      pos = output.find(token);
    }
  };

  eraseAll("<|im_start|>");
  eraseAll("<|im_end|>");
  eraseAll("<think>");
  eraseAll("</think>");

  const std::string assistantPrefix = "assistant\n";
  std::size_t prefixPos = output.find(assistantPrefix);
  if (prefixPos != std::string::npos) {
    output = output.substr(prefixPos + assistantPrefix.size());
  }

  while (!output.empty() &&
         (output.front() == '\n' || output.front() == '\r')) {
    output.erase(output.begin());
  }

  while (!output.empty() &&
         (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }

  return output;
}

bool IsEffectivelyEmptyModelOutput(const std::string& value) {
  if (value.empty()) {
    return true;
  }

  for (const char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isspace(c) != 0) {
      continue;
    }

    if (ch == '<' || ch == '>' || ch == '|' || ch == '\r' || ch == '\n') {
      continue;
    }

    return false;
  }

  return true;
}

std::vector<std::int64_t> FindSequenceTail(
    const std::vector<std::int64_t>& allIds,
    const std::vector<std::int64_t>& prefix) {
  if (allIds.empty()) {
    return {};
  }

  if (prefix.empty() || allIds.size() <= prefix.size()) {
    return allIds;
  }

  if (std::equal(prefix.begin(), prefix.end(), allIds.begin())) {
    return std::vector<std::int64_t>(
        allIds.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
        allIds.end());
  }

  return allIds;
}

bool IsInputNameContains(
    const std::string& value,
    const char* token) {
  if (token == nullptr || *token == '\0') {
    return false;
  }

  const std::string lowered = ToLowerAscii(value);
  return lowered.find(token) != std::string::npos;
}

bool IsPastKeyValueName(const std::string& inputName) {
  return IsInputNameContains(inputName, "past_key") ||
      IsInputNameContains(inputName, "past_value") ||
      IsInputNameContains(inputName, "past_key_values");
}

bool ResolveInputShape(
    const std::string& inputName,
    const std::vector<std::int64_t>& modelShape,
    const std::size_t sequenceLength,
    std::vector<std::int64_t>& outShape,
    std::size_t& outElementCount) {
  outShape.clear();
  outElementCount = 1;

  if (modelShape.empty()) {
    outShape.push_back(1);
    outShape.push_back(static_cast<std::int64_t>(sequenceLength));
    outElementCount = sequenceLength;
    return true;
  }

  const bool isInputIds = IsInputNameContains(inputName, "input_ids");
  const bool isAttentionMask = IsInputNameContains(inputName, "attention_mask");
  const bool isPositionIds = IsInputNameContains(inputName, "position");
  const bool isPast = IsPastKeyValueName(inputName);

  outShape.reserve(modelShape.size());
  for (std::size_t i = 0; i < modelShape.size(); ++i) {
    const std::int64_t sourceDim = modelShape[i];
    std::int64_t dim = sourceDim;

    if (dim <= 0) {
      if (i == 0) {
        dim = 1;
      } else if ((isInputIds || isAttentionMask || isPositionIds) && i == 1) {
        dim = static_cast<std::int64_t>(sequenceLength);
      } else if (isPast && i == 2) {
        dim = 0;
      } else {
        dim = 1;
      }
    }

    if (dim < 0) {
      return false;
    }

    if (dim > 0) {
      const std::size_t dimSize = static_cast<std::size_t>(dim);
      if (dimSize > 0 && outElementCount >
          (std::numeric_limits<std::size_t>::max)() / dimSize) {
        return false;
      }

      outElementCount *= dimSize;
    } else {
      outElementCount = 0;
    }

    outShape.push_back(dim);
  }

  return true;
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

  if (m_snapshot.tokenizerPath.empty()) {
    m_snapshot.ready = false;
    m_snapshot.status = "tokenizer_missing";
    ++m_snapshot.modelLoadFailures;
    m_snapshot.error = TextGenerationError{
        .code = TextGenerationErrorCode::TokenizerNotFound,
        .message = "chat.localModel.tokenizerPath is not configured.",
    };
    TraceRuntime(
        "model.load.failure",
        {},
        "status=tokenizer_missing reason=tokenizerPath_not_configured");
    return false;
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
    ConfigureDefaultSessionOptions(*m_sessionState->options);

    bool usingDirectMl = false;
    std::string directMlFallbackReason;
    if (TryAppendDirectMlExecutionProvider(
            *m_sessionState->options,
            directMlFallbackReason)) {
      usingDirectMl = true;
      m_snapshot.effectiveExecutionProvider = "directml";
      TraceRuntime(
          "model.execution_provider",
          {},
          "provider=directml");
    } else {
      m_snapshot.effectiveExecutionProvider = "cpu";
      TraceRuntime(
          "model.execution_provider",
          {},
          "provider=cpu fallbackReason=" + directMlFallbackReason);
    }

    try {
      m_sessionState->session = std::make_unique<Ort::Session>(
          *m_sessionState->env,
          modelPath.c_str(),
          *m_sessionState->options);
    } catch (const std::exception& ex) {
      if (!usingDirectMl) {
        throw;
      }

      directMlFallbackReason =
          "directml_session_init_failed: " + std::string(ex.what());
      m_snapshot.effectiveExecutionProvider = "cpu";
      TraceRuntime(
          "model.execution_provider",
          {},
          "provider=cpu fallbackReason=" + directMlFallbackReason);

      m_sessionState->options = std::make_unique<Ort::SessionOptions>();
      ConfigureDefaultSessionOptions(*m_sessionState->options);
      m_sessionState->session = std::make_unique<Ort::Session>(
          *m_sessionState->env,
          modelPath.c_str(),
          *m_sessionState->options);
    }

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
  m_snapshot.effectiveExecutionProvider = "cpu";
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
  std::vector<std::int64_t> inputTokenIds = m_tokenizer.EncodeToIds(
      request.prompt,
      maxTokens,
      tokenizationError,
      true);
  if (inputTokenIds.empty()) {
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

  std::vector<std::int64_t> generatedTokenIds;
  generatedTokenIds.reserve(maxTokens);
  bool firstTokenLogged = false;

  std::int64_t imEndTokenId = -1;
  const bool hasImEndToken = m_tokenizer.TryGetTokenId(
      "<|im_end|>",
      imEndTokenId);

#if BLAZECLAW_HAS_ONNXRUNTIME
  std::uint32_t generatedCount = 0;
  while (generatedCount < maxTokens) {
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
      result.generatedTokens = static_cast<std::uint32_t>(generatedTokenIds.size());
      result.error = TextGenerationError{
          .code = TextGenerationErrorCode::Cancelled,
          .message = "Generation cancelled.",
      };
      result.latencyMs = ElapsedMs(startedAt);
      return result;
    }

    std::vector<std::int64_t> attentionMask(
        inputTokenIds.size(),
        1);
    std::vector<std::int64_t> positionIds = BuildPositionIds(
        inputTokenIds.size());

    std::vector<const char*> inputNames;
    std::vector<std::string> inputNameStorage;
    std::vector<Ort::Value> inputTensors;
    std::vector<std::vector<std::int64_t>> int64Storage;
    std::vector<std::vector<std::int32_t>> int32Storage;
    std::vector<std::vector<Ort::Float16_t>> float16Storage;

    std::vector<const char*> outputNames;
    std::vector<std::string> outputNameStorage;
    std::size_t logitsOutputIndex = 0;

    try {
      Ort::AllocatorWithDefaultOptions allocator;
      const auto memoryInfo = Ort::MemoryInfo::CreateCpu(
          OrtArenaAllocator,
          OrtMemTypeDefault);

      const std::size_t inputCount = m_sessionState->session->GetInputCount();
      inputNames.reserve(inputCount);
      inputNameStorage.reserve(inputCount);
      inputTensors.reserve(inputCount);

      for (std::size_t i = 0; i < inputCount; ++i) {
        auto nameAllocated = m_sessionState->session->GetInputNameAllocated(
            i,
            allocator);
        const char* rawName = nameAllocated.get();
        if (rawName == nullptr) {
          continue;
        }

        const std::string inputName = rawName;
        inputNameStorage.push_back(inputName);
        inputNames.push_back(inputNameStorage.back().c_str());

        auto inputType = m_sessionState->session->GetInputTypeInfo(i);
        if (inputType.GetONNXType() != ONNX_TYPE_TENSOR) {
          continue;
        }

        auto tensorInfo = inputType.GetTensorTypeAndShapeInfo();
        std::vector<std::int64_t> resolvedShape;
        std::size_t elementCount = 0;
        if (!ResolveInputShape(
                inputName,
                tensorInfo.GetShape(),
                inputTokenIds.size(),
                resolvedShape,
                elementCount)) {
          result.ok = false;
          result.modelId = m_snapshot.modelPath;
          result.error = TextGenerationError{
              .code = TextGenerationErrorCode::InferenceFailed,
              .message = "Unable to resolve ONNX input shape.",
          };
          result.latencyMs = ElapsedMs(startedAt);
          return result;
        }

        const bool isInputIds = IsInputNameContains(inputName, "input_ids");
        const bool isAttentionMask = IsInputNameContains(inputName, "attention_mask");
        const bool isPositionIds = IsInputNameContains(inputName, "position");

        std::vector<std::int64_t> sourceValues(elementCount, 0);
        const auto copyCount = (std::min)(
            sourceValues.size(),
            inputTokenIds.size());
        if (isInputIds) {
          std::copy_n(
              inputTokenIds.data(),
              copyCount,
              sourceValues.data());
        } else if (isAttentionMask) {
          std::copy_n(
              attentionMask.data(),
              copyCount,
              sourceValues.data());
        } else if (isPositionIds) {
          std::copy_n(
              positionIds.data(),
              copyCount,
              sourceValues.data());
        }

        const auto elementType = tensorInfo.GetElementType();
        if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
          int64Storage.push_back(std::move(sourceValues));
          auto& values = int64Storage.back();
          inputTensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
              memoryInfo,
              values.data(),
              values.size(),
              resolvedShape.data(),
              resolvedShape.size()));
          continue;
        }

        if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
          int32Storage.emplace_back();
          auto& values = int32Storage.back();
          values.reserve(sourceValues.size());
          for (const auto value : sourceValues) {
            values.push_back(static_cast<std::int32_t>(value));
          }

          inputTensors.push_back(Ort::Value::CreateTensor<std::int32_t>(
              memoryInfo,
              values.data(),
              values.size(),
              resolvedShape.data(),
              resolvedShape.size()));
          continue;
        }

        if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
          float16Storage.emplace_back();
          auto& values = float16Storage.back();
          values.reserve(sourceValues.size());
          for (const auto value : sourceValues) {
            values.emplace_back(static_cast<float>(value));
          }

          inputTensors.push_back(Ort::Value::CreateTensor<Ort::Float16_t>(
              memoryInfo,
              values.data(),
              values.size(),
              resolvedShape.data(),
              resolvedShape.size()));
          continue;
        }

        int64Storage.emplace_back(sourceValues.size(), 0);
        auto& values = int64Storage.back();
        inputTensors.push_back(Ort::Value::CreateTensor<std::int64_t>(
            memoryInfo,
            values.data(),
            values.size(),
            resolvedShape.data(),
            resolvedShape.size()));
      }

      const std::size_t outputCount = m_sessionState->session->GetOutputCount();
      outputNames.reserve(outputCount);
      outputNameStorage.reserve(outputCount);
      for (std::size_t i = 0; i < outputCount; ++i) {
        auto nameAllocated = m_sessionState->session->GetOutputNameAllocated(
            i,
            allocator);
        const char* rawName = nameAllocated.get();
        if (rawName == nullptr) {
          continue;
        }

        outputNameStorage.push_back(rawName);
        outputNames.push_back(outputNameStorage.back().c_str());
        if (IsInputNameContains(outputNameStorage.back(), "logits")) {
          logitsOutputIndex = outputNames.size() - 1;
        }
      }

      auto outputs = m_sessionState->session->Run(
          Ort::RunOptions{nullptr},
          inputNames.data(),
          inputTensors.data(),
          inputTensors.size(),
          outputNames.data(),
          outputNames.size());
      if (outputs.empty() || logitsOutputIndex >= outputs.size()) {
        result.ok = false;
        result.modelId = m_snapshot.modelPath;
        result.error = TextGenerationError{
            .code = TextGenerationErrorCode::InferenceFailed,
            .message = "ONNX inference produced no logits output.",
        };
        result.latencyMs = ElapsedMs(startedAt);
        return result;
      }

      auto& logitsValue = outputs[logitsOutputIndex];
      if (!logitsValue.IsTensor()) {
        result.ok = false;
        result.modelId = m_snapshot.modelPath;
        result.error = TextGenerationError{
            .code = TextGenerationErrorCode::InferenceFailed,
            .message = "ONNX logits output is not a tensor.",
        };
        result.latencyMs = ElapsedMs(startedAt);
        return result;
      }

      auto logitsInfo = logitsValue.GetTensorTypeAndShapeInfo();
      const auto logitsShape = logitsInfo.GetShape();
      if (logitsShape.size() < 3) {
        result.ok = false;
        result.modelId = m_snapshot.modelPath;
        result.error = TextGenerationError{
            .code = TextGenerationErrorCode::InferenceFailed,
            .message = "ONNX logits output shape is unsupported.",
        };
        result.latencyMs = ElapsedMs(startedAt);
        return result;
      }

      const std::size_t sequenceLength = static_cast<std::size_t>(
          (std::max)(std::int64_t{1}, logitsShape[1]));
      const std::size_t vocabSize = static_cast<std::size_t>(
          (std::max)(std::int64_t{1}, logitsShape[2]));
      const std::size_t baseOffset = (sequenceLength - 1) * vocabSize;

      std::vector<float> logitsFloat;
      const float* logitsData = nullptr;
      if (logitsInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        logitsData = logitsValue.GetTensorData<float>();
      } else if (
          logitsInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16) {
        const auto* logitsData16 = logitsValue.GetTensorData<Ort::Float16_t>();
        logitsFloat.reserve(vocabSize);
        for (std::size_t i = 0; i < vocabSize; ++i) {
          logitsFloat.push_back(logitsData16[baseOffset + i].ToFloat());
        }
      } else {
        result.ok = false;
        result.modelId = m_snapshot.modelPath;
        result.error = TextGenerationError{
            .code = TextGenerationErrorCode::InferenceFailed,
            .message = "ONNX logits tensor type is unsupported.",
        };
        result.latencyMs = ElapsedMs(startedAt);
        return result;
      }

      float maxLogit = -std::numeric_limits<float>::infinity();
      std::size_t maxIndex = 0;
      for (std::size_t i = 0; i < vocabSize; ++i) {
        const float value = logitsData != nullptr
            ? logitsData[baseOffset + i]
            : logitsFloat[i];
        if (i == 0 || value > maxLogit) {
          maxLogit = value;
          maxIndex = i;
        }
      }

      const std::int64_t nextTokenId = static_cast<std::int64_t>(maxIndex);
      if (m_tokenizer.IsEndOfSequenceId(nextTokenId) ||
          nextTokenId == m_tokenizer.BosTokenId() ||
          (hasImEndToken && nextTokenId == imEndTokenId)) {
        break;
      }

      inputTokenIds.push_back(nextTokenId);
      generatedTokenIds.push_back(nextTokenId);
      ++generatedCount;

      if (!firstTokenLogged) {
        TraceRuntime(
            "request.first_token",
            request.runId,
            "tokenIndex=" + std::to_string(generatedTokenIds.size()));
        firstTokenLogged = true;
      }

      if (onDelta) {
        const std::string decodedRaw = m_tokenizer.DecodeFromIds(generatedTokenIds);
        const std::string decoded = StripQwenThinkingAndControlTokens(decodedRaw);
        const std::string previous = result.text;
        result.text = decoded;
        const std::string delta = decoded.size() > previous.size()
            ? decoded.substr(previous.size())
            : std::string();
        onDelta(delta);
      }
    } catch (const Ort::Exception& ex) {
      result.ok = false;
      result.modelId = m_snapshot.modelPath;
      result.error = TextGenerationError{
          .code = TextGenerationErrorCode::InferenceFailed,
          .message = ex.what(),
      };
      result.latencyMs = ElapsedMs(startedAt);
      return result;
    }
  }
#else
  result.ok = false;
  result.modelId = m_snapshot.modelPath;
  result.error = TextGenerationError{
      .code = TextGenerationErrorCode::RuntimeUnavailable,
      .message = "ONNX Runtime is unavailable at compile time.",
  };
  result.latencyMs = ElapsedMs(startedAt);
  return result;
#endif

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!request.runId.empty()) {
      m_cancelFlagsByRunId.erase(request.runId);
    }
  }

  result.ok = true;
  result.cancelled = false;
  result.modelId = m_snapshot.modelPath;
  const auto decodeTailIds = FindSequenceTail(
      generatedTokenIds,
      inputTokenIds);
  result.generatedTokens = static_cast<std::uint32_t>(decodeTailIds.size());
  result.text = StripQwenThinkingAndControlTokens(
      m_tokenizer.DecodeFromIds(decodeTailIds));
  if (IsEffectivelyEmptyModelOutput(result.text)) {
    result.ok = false;
    result.generatedTokens = 0;
    result.error = TextGenerationError{
        .code = TextGenerationErrorCode::EmptyOutput,
        .message = "local model returned empty output after decode normalization",
    };
    result.latencyMs = ElapsedMs(startedAt);
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      ++m_snapshot.requestsFailed;
      m_snapshot.lastLatencyMs = result.latencyMs;
    }
    TraceRuntime(
        "request.terminal",
        request.runId,
        "state=error reason=local_model_empty_output latencyMs=" +
            std::to_string(result.latencyMs));
    return result;
  }

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
  m_snapshot.effectiveExecutionProvider = "unknown";
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
