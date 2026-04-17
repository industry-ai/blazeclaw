#include "pch.h"
#include "LlamaTextGenerationRuntime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>

#if __has_include(<llama.h>)
#include <llama.h>
#define BLAZECLAW_HAS_LLAMACPP 1
#else
#define BLAZECLAW_HAS_LLAMACPP 0
#endif

namespace blazeclaw::core::localmodel {

	namespace {

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

	} // namespace

	struct LlamaTextGenerationRuntime::SessionState {
		bool loaded = false;
	};

	LlamaTextGenerationRuntime::LlamaTextGenerationRuntime() {
		m_sessionState = std::make_unique<SessionState>();
		ResetSnapshotLocked();
	}

	LlamaTextGenerationRuntime::~LlamaTextGenerationRuntime() = default;

	void LlamaTextGenerationRuntime::Configure(
		const blazeclaw::config::AppConfig& appConfig) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = appConfig;
		m_sessionState = std::make_unique<SessionState>();
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
		m_sessionState = std::make_unique<SessionState>();
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

		const std::string placeholder =
			"llama.cpp runtime adapter is active. Native token generation "
			"backend wiring will execute here.";
		std::vector<std::string> chunks;
		std::size_t cursor = 0;
		while (cursor < placeholder.size()) {
			const std::size_t nextSpace = placeholder.find(' ', cursor);
			if (nextSpace == std::string::npos) {
				chunks.push_back(placeholder.substr(cursor));
				break;
			}

			chunks.push_back(placeholder.substr(cursor, nextSpace - cursor + 1));
			cursor = nextSpace + 1;
		}

		std::string generated;
		std::uint32_t generatedTokens = 0;
		const std::uint32_t maxTokens =
			request.maxTokens.has_value() ? request.maxTokens.value() : m_snapshot.maxTokens;

		for (const auto& chunk : chunks) {
			bool cancelled = false;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!request.runId.empty()) {
					const auto it = m_cancelFlagsByRunId.find(request.runId);
					cancelled = it != m_cancelFlagsByRunId.end() && it->second;
				}
			}

			if (cancelled) {
				result.ok = false;
				result.cancelled = true;
				result.modelId = m_snapshot.modelPath;
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

			if (generatedTokens >= maxTokens) {
				break;
			}

			generated += chunk;
			++generatedTokens;
			if (onDelta) {
				onDelta(generated);
			}
		}

		result.ok = !generated.empty();
		result.text = generated;
		result.modelId = m_snapshot.modelPath;
		result.generatedTokens = generatedTokens;
		result.latencyMs = ElapsedMs(startedAt);
		if (!result.ok) {
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::EmptyOutput,
				.message = "No tokens were generated.",
			};
		}

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
