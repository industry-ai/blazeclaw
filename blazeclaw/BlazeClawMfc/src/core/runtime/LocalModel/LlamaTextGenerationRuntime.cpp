#include "pch.h"
#include "LlamaTextGenerationRuntime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

#if __has_include(<llama.h>)
#include <llama.h>
#define BLAZECLAW_HAS_LLAMACPP 1
#else
#define BLAZECLAW_HAS_LLAMACPP 0
#endif

namespace blazeclaw::core::localmodel {

	namespace {

		struct BackendInitGuard {
			BackendInitGuard() {
#if BLAZECLAW_HAS_LLAMACPP
				llama_backend_init();
#endif
			}

			~BackendInitGuard() {
#if BLAZECLAW_HAS_LLAMACPP
				llama_backend_free();
#endif
			}
		};

		BackendInitGuard g_backendInitGuard{};

		#if BLAZECLAW_HAS_LLAMACPP
				struct SamplerHolder {
					llama_sampler* value = nullptr;
					~SamplerHolder() {
						if (value != nullptr) {
							llama_sampler_free(value);
							value = nullptr;
						}
					}
				};
		#endif

		std::string ToNarrow(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		std::string ToLowerAscii(const std::string& value) {
			std::string lowered = value;
			for (char& ch : lowered) {
				if (ch >= 'A' && ch <= 'Z') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}

			return lowered;
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

			std::filesystem::path root(storageRoot);
			if (root.empty()) {
				return configured.lexically_normal();
			}

			if (!root.is_absolute()) {
				std::error_code ec;
				root = std::filesystem::current_path(ec) / root;
			}

			return (root / configured).lexically_normal();
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

		std::string TrimAscii(const std::string& value) {
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		std::uint32_t ApproximateGeneratedTokens(const std::string& text) {
			const auto trimmed = TrimAscii(text);
			if (trimmed.empty()) {
				return 0;
			}

			std::istringstream stream(trimmed);
			std::string token;
			std::uint32_t count = 0;
			while (stream >> token) {
				++count;
			}
			return count > 0 ? count : 1;
		}

		#if BLAZECLAW_HAS_LLAMACPP
				std::vector<llama_token> TokenizePrompt(
					const llama_vocab* vocab,
					const std::string& prompt) {
					if (vocab == nullptr || prompt.empty()) {
						return {};
					}

					const int32_t requested = static_cast<int32_t>(prompt.size()) + 8;
					std::vector<llama_token> tokens(static_cast<std::size_t>(requested));
					int32_t count = llama_tokenize(
						vocab,
						prompt.c_str(),
						static_cast<int32_t>(prompt.size()),
						tokens.data(),
						requested,
						true,
						false);
					if (count < 0) {
						const int32_t needed = -count;
						tokens.assign(static_cast<std::size_t>(needed), 0);
						count = llama_tokenize(
							vocab,
							prompt.c_str(),
							static_cast<int32_t>(prompt.size()),
							tokens.data(),
							needed,
							true,
							false);
					}

					if (count <= 0) {
						return {};
					}

					tokens.resize(static_cast<std::size_t>(count));
					return tokens;
				}

				std::string TokenToText(const llama_vocab* vocab, const llama_token token) {
					if (vocab == nullptr) {
						return {};
					}

					char piece[512]{};
					const int32_t pieceLen = llama_token_to_piece(
						vocab,
						token,
						piece,
						static_cast<int32_t>(sizeof(piece)),
						0,
						true);
					if (pieceLen <= 0) {
						return {};
					}

					return std::string(piece, piece + pieceLen);
				}
		#endif

	} // namespace

	struct LlamaTextGenerationRuntime::SessionState {
		bool loaded = false;
#if BLAZECLAW_HAS_LLAMACPP
		llama_model* model = nullptr;
		llama_context* context = nullptr;
		const llama_vocab* vocab = nullptr;
#endif

		~SessionState() {
#if BLAZECLAW_HAS_LLAMACPP
			if (context != nullptr) {
				llama_free(context);
				context = nullptr;
			}
			if (model != nullptr) {
				llama_model_free(model);
				model = nullptr;
			}
			vocab = nullptr;
#endif
		}
	};

	LlamaTextGenerationRuntime::LlamaTextGenerationRuntime() {
		m_sessionState = std::make_shared<SessionState>();
		ResetSnapshotLocked();
	}

	LlamaTextGenerationRuntime::~LlamaTextGenerationRuntime() = default;

	void LlamaTextGenerationRuntime::Configure(
		const blazeclaw::config::AppConfig& appConfig) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = appConfig;
		m_sessionState = std::make_shared<SessionState>();
		m_cancelFlagsByRunId.clear();
		ResetSnapshotLocked();
	}

	LocalModelRuntimeSnapshot LlamaTextGenerationRuntime::Snapshot() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void LlamaTextGenerationRuntime::TraceRuntime(
		const char* stage,
		const std::string& runId,
		const std::string& details) {
		TRACE(
			"[LocalModel][llama.cpp] %s runId=%s %s\n",
			stage == nullptr ? "<null>" : stage,
			runId.c_str(),
			details.c_str());
	}

	void LlamaTextGenerationRuntime::ResetSnapshotLocked() {
		m_snapshot = LocalModelRuntimeSnapshot{};
		m_snapshot.enabled = m_config.localModel.enabled;
		m_snapshot.provider = ToNarrow(m_config.localModel.provider);
		m_snapshot.rolloutStage = ToNarrow(m_config.localModel.rolloutStage);
		m_snapshot.storageRoot = ToNarrow(m_config.localModel.storageRoot);
		m_snapshot.version = ToNarrow(m_config.localModel.version);
		m_snapshot.modelPath = ToNarrow(m_config.localModel.modelPath);
		m_snapshot.modelExpectedSha256 = ToNarrow(m_config.localModel.modelSha256);
		m_snapshot.tokenizerPath = ToNarrow(m_config.localModel.tokenizerPath);
		m_snapshot.tokenizerExpectedSha256 =
			ToNarrow(m_config.localModel.tokenizerSha256);
		m_snapshot.maxTokens = m_config.localModel.maxTokens;
		m_snapshot.temperature = m_config.localModel.temperature;
		m_snapshot.intraThreads = m_config.localModel.intraThreads;
		m_snapshot.interThreads = m_config.localModel.interThreads;
		m_snapshot.executionMode = ToNarrow(m_config.localModel.executionMode);
		m_snapshot.verboseMetrics = m_config.localModel.verboseMetrics;
		m_snapshot.status = m_snapshot.enabled ? "configured" : "disabled";
	}

	bool LlamaTextGenerationRuntime::LoadModel() {
		std::lock_guard<std::mutex> lock(m_mutex);
		ResetSnapshotLocked();
		++m_snapshot.modelLoadAttempts;

		m_snapshot.modelPath = ResolveConfiguredPath(
			m_snapshot.modelPath,
			m_snapshot.storageRoot)
			.string();

		const std::string provider = ToLowerAscii(m_snapshot.provider);
		const bool providerSupported =
			provider == "llama.cpp" || provider == "llama";

		TraceRuntime(
			"model.load.start",
			{},
			"provider=" + m_snapshot.provider +
			" storageRoot=" + m_snapshot.storageRoot +
			" model=" + m_snapshot.modelPath);

		if (!m_snapshot.enabled) {
			m_snapshot.ready = false;
			m_snapshot.status = "disabled";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::LocalModelDisabled,
				.message = "chat.localModel.enabled=false",
			};
			return false;
		}

		if (!providerSupported) {
			m_snapshot.ready = false;
			m_snapshot.status = "provider_not_supported";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::ProviderNotSupported,
				.message = "Local llama runtime requires provider llama.cpp.",
			};
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
				.message = "Local GGUF model file was not found.",
			};
			return false;
		}

#if BLAZECLAW_HAS_LLAMACPP
		auto nextSession = std::make_shared<SessionState>();

		llama_model_params modelParams = llama_model_default_params();
		modelParams.n_gpu_layers = m_config.localModel.llama.gpuLayers;
		modelParams.use_mmap = true;
		modelParams.use_mlock = false;

		nextSession->model =
			llama_model_load_from_file(m_snapshot.modelPath.c_str(), modelParams);
		if (nextSession->model == nullptr) {
			m_snapshot.ready = false;
			m_snapshot.status = "model_load_failed";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message = "Failed to load GGUF model with llama_model_load_from_file.",
			};
			TraceRuntime("model.load.failure", {}, "status=model_load_failed");
			return false;
		}

		llama_context_params contextParams = llama_context_default_params();
		contextParams.n_ctx = m_config.localModel.llama.contextLength;
		contextParams.n_batch = m_config.localModel.llama.batchSize;
		contextParams.n_ubatch = m_config.localModel.llama.batchSize;
		contextParams.n_threads = static_cast<int32_t>(m_config.localModel.llama.threads);
		contextParams.n_threads_batch = static_cast<int32_t>(m_config.localModel.llama.threads);
		contextParams.embeddings = false;
		contextParams.no_perf = !m_config.localModel.llama.verboseMetrics;
		contextParams.flash_attn_type =
			m_config.localModel.llama.flashAttention
			? LLAMA_FLASH_ATTN_TYPE_ENABLED
			: LLAMA_FLASH_ATTN_TYPE_DISABLED;

		nextSession->context = llama_init_from_model(nextSession->model, contextParams);
		if (nextSession->context == nullptr) {
			m_snapshot.ready = false;
			m_snapshot.status = "context_init_failed";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message = "Failed to initialize llama context from model.",
			};
			TraceRuntime("model.load.failure", {}, "status=context_init_failed");
			return false;
		}

		nextSession->vocab = llama_model_get_vocab(nextSession->model);
		if (nextSession->vocab == nullptr) {
			m_snapshot.ready = false;
			m_snapshot.status = "vocab_init_failed";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message = "Failed to resolve llama vocabulary from loaded model.",
			};
			TraceRuntime("model.load.failure", {}, "status=vocab_init_failed");
			return false;
		}

		m_sessionState = std::move(nextSession);
		m_sessionState->loaded = true;
		m_snapshot.runtimeDllPresent = true;
		m_snapshot.effectiveExecutionProvider = "llama.cpp";
		m_snapshot.ready = true;
		m_snapshot.status = "ready";
		m_snapshot.error.reset();
		TraceRuntime(
			"model.load.success",
			{},
			"status=ready model=" + m_snapshot.modelPath);
		return true;
#else
		m_snapshot.ready = false;
		m_snapshot.status = "runtime_unavailable";
		++m_snapshot.modelLoadFailures;
		m_snapshot.error = TextGenerationError{
			.code = TextGenerationErrorCode::RuntimeUnavailable,
			.message =
				"llama.cpp headers are unavailable at compile time. Build third_party/llama.cpp artifacts and recompile.",
		};
		TraceRuntime(
			"model.load.failure",
			{},
			"status=runtime_unavailable reason=llama_headers_missing");
		return false;
#endif
	}

	bool LlamaTextGenerationRuntime::EnsureLoadedLocked(
		TextGenerationResult& outResult) {
		if (m_snapshot.ready && m_sessionState && m_sessionState->loaded) {
			return true;
		}

		outResult.ok = false;
		outResult.modelId = m_snapshot.modelPath;
		outResult.error = m_snapshot.error.has_value()
			? m_snapshot.error
			: std::optional<TextGenerationError>(TextGenerationError{
				  .code = TextGenerationErrorCode::RuntimeUnavailable,
				  .message = "local llama runtime is not ready",
				});
		return false;
	}

	TextGenerationResult LlamaTextGenerationRuntime::GenerateStream(
		const TextGenerationRequest& request,
		const TextDeltaCallback& onDelta) {
		std::lock_guard<std::mutex> generationLock(m_generationMutex);
		const auto startedAt = std::chrono::steady_clock::now();
		TextGenerationResult result;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_snapshot.requestsStarted;
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
				return result;
			}

			if (!request.runId.empty()) {
				m_cancelFlagsByRunId.insert_or_assign(request.runId, false);
			}
		}

		const auto clearCancelFlag = [this, &request](void*) {
			if (request.runId.empty()) {
				return;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			m_cancelFlagsByRunId.erase(request.runId);
			};
		std::unique_ptr<void, decltype(clearCancelFlag)> cancelFlagGuard(
			reinterpret_cast<void*>(1),
			clearCancelFlag);

		std::string modelPath;
		std::uint32_t maxTokens = 0;
		double temperature = 0.0;
		auto sessionState = std::shared_ptr<SessionState>{};
#if BLAZECLAW_HAS_LLAMACPP
		llama_context* context = nullptr;
		const llama_vocab* vocab = nullptr;
#endif
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			modelPath = m_snapshot.modelPath;
			maxTokens = request.maxTokens.has_value() ? request.maxTokens.value() : m_snapshot.maxTokens;
			temperature = request.temperature.has_value() ? request.temperature.value() : m_snapshot.temperature;
			sessionState = m_sessionState;
#if BLAZECLAW_HAS_LLAMACPP
			if (sessionState) {
				context = sessionState->context;
				vocab = sessionState->vocab;
			}
#endif
		}

		auto isCancelled = [this, &request]() {
			if (request.runId.empty()) {
				return false;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_cancelFlagsByRunId.find(request.runId);
			return it != m_cancelFlagsByRunId.end() && it->second;
		};

		if (isCancelled()) {
			result.ok = false;
			result.cancelled = true;
			result.modelId = modelPath;
			result.generatedTokens = 0;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::Cancelled,
				.message = "Generation cancelled.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsCancelled;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

#if !BLAZECLAW_HAS_LLAMACPP
		result.ok = false;
		result.modelId = modelPath;
		result.error = TextGenerationError{
			.code = TextGenerationErrorCode::RuntimeUnavailable,
			.message = "llama.cpp headers are unavailable at compile time.",
		};
		result.latencyMs = ElapsedMs(startedAt);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_snapshot.requestsFailed;
			m_snapshot.lastLatencyMs = result.latencyMs;
			m_snapshot.lastGeneratedTokens = 0;
		}
		return result;
#else
		if (context == nullptr || vocab == nullptr) {
			result.ok = false;
			result.modelId = modelPath;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message = "llama runtime session is not initialized.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		const auto promptTokens = TokenizePrompt(vocab, request.prompt);
		if (promptTokens.empty()) {
			result.ok = false;
			result.modelId = modelPath;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::InferenceFailed,
				.message = "Failed to tokenize prompt for llama runtime.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		if (maxTokens == 0) {
			maxTokens = 1;
		}

		llama_memory_clear(llama_get_memory(context), false);
		auto promptBatch = llama_batch_get_one(
			const_cast<llama_token*>(promptTokens.data()),
			static_cast<int32_t>(promptTokens.size()));
		if (llama_decode(context, promptBatch) != 0) {
			result.ok = false;
			result.modelId = modelPath;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::InferenceFailed,
				.message = "llama_decode failed during prompt prefill.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		SamplerHolder samplerHolder;
		auto samplerParams = llama_sampler_chain_default_params();
		samplerHolder.value = llama_sampler_chain_init(samplerParams);
		if (samplerHolder.value == nullptr) {
			result.ok = false;
			result.modelId = modelPath;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message = "Failed to initialize llama sampler chain.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		if (temperature <= 0.0) {
			llama_sampler_chain_add(samplerHolder.value, llama_sampler_init_greedy());
		}
		else {
			llama_sampler_chain_add(samplerHolder.value, llama_sampler_init_top_k(40));
			llama_sampler_chain_add(samplerHolder.value, llama_sampler_init_top_p(0.95f, 1));
			llama_sampler_chain_add(samplerHolder.value, llama_sampler_init_temp(static_cast<float>(temperature)));
			std::uint32_t seed = 0;
			for (const char ch : request.runId) {
				seed = (seed * 131u) + static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
			}
			if (seed == 0) {
				seed = static_cast<std::uint32_t>(
					std::chrono::steady_clock::now().time_since_epoch().count());
			}
			llama_sampler_chain_add(samplerHolder.value, llama_sampler_init_dist(seed));
		}

		std::string generated;
		std::uint32_t generatedTokens = 0;
		for (; generatedTokens < maxTokens; ++generatedTokens) {
			if (isCancelled()) {
				result.ok = false;
				result.cancelled = true;
				result.modelId = modelPath;
				result.generatedTokens = generatedTokens;
				result.error = TextGenerationError{
					.code = TextGenerationErrorCode::Cancelled,
					.message = "Generation cancelled.",
				};
				result.latencyMs = ElapsedMs(startedAt);
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					++m_snapshot.requestsCancelled;
					m_snapshot.lastLatencyMs = result.latencyMs;
					m_snapshot.lastGeneratedTokens = generatedTokens;
				}
				return result;
			}

			const llama_token next = llama_sampler_sample(samplerHolder.value, context, -1);
			if (next == llama_vocab_eos(vocab) || llama_vocab_is_eog(vocab, next)) {
				break;
			}

			llama_sampler_accept(samplerHolder.value, next);
			const auto piece = TokenToText(vocab, next);
			if (!piece.empty()) {
				generated += piece;
				if (onDelta) {
					onDelta(generated);
				}
			}

			llama_token generatedToken = next;
			auto stepBatch = llama_batch_get_one(&generatedToken, 1);
			if (llama_decode(context, stepBatch) != 0) {
				result.ok = false;
				result.modelId = modelPath;
				result.generatedTokens = generatedTokens;
				result.error = TextGenerationError{
					.code = TextGenerationErrorCode::InferenceFailed,
					.message = "llama_decode failed during token generation.",
				};
				result.latencyMs = ElapsedMs(startedAt);
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					++m_snapshot.requestsFailed;
					m_snapshot.lastLatencyMs = result.latencyMs;
					m_snapshot.lastGeneratedTokens = generatedTokens;
				}
				return result;
			}
		}

		const auto trimmedGenerated = TrimAscii(generated);
		const std::uint32_t finalTokens = ApproximateGeneratedTokens(trimmedGenerated);
		result.ok = !trimmedGenerated.empty();
		result.text = trimmedGenerated;
		result.modelId = modelPath;
		result.generatedTokens = finalTokens;
		result.latencyMs = ElapsedMs(startedAt);
		if (!result.ok) {
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::EmptyOutput,
				.message = "No tokens were generated.",
			};
		}
#endif

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (result.ok) {
				++m_snapshot.requestsCompleted;
				m_snapshot.cumulativeTokens += result.generatedTokens;
				m_snapshot.cumulativeLatencyMs += result.latencyMs;
			}
			else {
				++m_snapshot.requestsFailed;
			}

			m_snapshot.lastLatencyMs = result.latencyMs;
			m_snapshot.lastGeneratedTokens = result.generatedTokens;
			m_snapshot.lastTokensPerSecond =
				TokensPerSecond(result.generatedTokens, result.latencyMs);
		}

		return result;
	}

	bool LlamaTextGenerationRuntime::Cancel(const std::string& runId) {
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

	bool LlamaTextGenerationRuntime::VerifyDeterministicContract(
		std::string& outFailureReason) {
		outFailureReason.clear();

		const auto baseline = Snapshot();
		if (!baseline.enabled) {
			outFailureReason = "local model disabled";
			return false;
		}

		if (!baseline.ready) {
			outFailureReason = "local llama runtime not ready";
			return false;
		}

		const auto result = GenerateStream(
			TextGenerationRequest{
				.runId = "phase2-llama-det-contract",
				.prompt = "Respond with pong.",
				.maxTokens = 8,
				.temperature = 0.0,
			},
			nullptr);
		if (!result.ok || result.text.empty()) {
			outFailureReason = result.error.has_value() ? result.error->message : "deterministic generation failed";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core::localmodel
