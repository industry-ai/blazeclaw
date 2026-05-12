#include "pch.h"
#include "SpeechRecognitionRuntime.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

namespace blazeclaw::core::speechrecognition {

	namespace {

		std::string ToNarrowLocal(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return output;
		}

		std::string NormalizeProvider(const std::wstring& value) {
			return ToNarrowLocal(value.empty() ? L"onnx" : value);
		}

		std::string NormalizeExecutionMode(const std::wstring& value) {
			const std::string mode = ToNarrowLocal(value);
			return mode == "parallel" ? "parallel" : "sequential";
		}

	} // namespace

	struct SpeechRecognitionRuntime::SessionState {
#if BLAZECLAW_HAS_ONNXRUNTIME
		std::unique_ptr<Ort::Env> env;
		std::unique_ptr<Ort::SessionOptions> options;
		std::unique_ptr<Ort::Session> session;
#endif
		bool initialized = false;
	};

	SpeechRecognitionRuntime::SpeechRecognitionRuntime() = default;
	SpeechRecognitionRuntime::~SpeechRecognitionRuntime() = default;

	void SpeechRecognitionRuntime::Configure(const blazeclaw::config::AppConfig& appConfig) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = appConfig;
		ResetSnapshotLocked();
	}

	SpeechRecognitionRuntimeSnapshot SpeechRecognitionRuntime::Snapshot() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void SpeechRecognitionRuntime::ResetSnapshotLocked() {
		m_sessionState = std::make_unique<SessionState>();
		m_cancelFlagsByRunId.clear();
		m_snapshot = SpeechRecognitionRuntimeSnapshot{};
		m_snapshot.enabled = m_config.speechRecognition.enabled;
		m_snapshot.provider = NormalizeProvider(m_config.speechRecognition.provider);
		m_snapshot.modelPath = ToNarrow(m_config.speechRecognition.modelPath);
		m_snapshot.language = ToNarrow(m_config.speechRecognition.language);
		m_snapshot.sampleRate = m_config.speechRecognition.sampleRate;
		m_snapshot.threads = m_config.speechRecognition.threads;
		m_snapshot.executionMode = NormalizeExecutionMode(m_config.speechRecognition.executionMode);
		m_snapshot.verboseMetrics = m_config.speechRecognition.verboseMetrics;
		m_snapshot.status = "configured";
	}

	bool SpeechRecognitionRuntime::EnsureLoadedLocked(SpeechTranscribeResult& outResult) {
		if (!m_sessionState) {
			m_sessionState = std::make_unique<SessionState>();
		}

		if (!m_snapshot.enabled) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::SpeechRecognitionDisabled,
				.message = "speech recognition is disabled",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "disabled";
			m_snapshot.error = outResult.error;
			return false;
		}

		if (m_snapshot.provider != "onnx") {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ProviderNotSupported,
				.message = "only onnx provider is supported",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "unsupported_provider";
			m_snapshot.error = outResult.error;
			return false;
		}

		if (m_snapshot.modelPath.empty() || !std::filesystem::exists(m_config.speechRecognition.modelPath)) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelNotFound,
				.message = "speech model path is not available",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_missing";
			m_snapshot.error = outResult.error;
			return false;
		}

#if !BLAZECLAW_HAS_ONNXRUNTIME
		outResult.ok = false;
		outResult.error = SpeechRecognitionError{
			.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
			.message = "onnxruntime headers not available at compile time",
		};
		m_snapshot.ready = false;
		m_snapshot.status = "runtime_unavailable";
		m_snapshot.error = outResult.error;
		return false;
#else
		if (m_sessionState->initialized) {
			return true;
		}

		try {
			m_sessionState->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "blazeclaw-speech");
			m_sessionState->options = std::make_unique<Ort::SessionOptions>();
			if (m_snapshot.threads > 0) {
				m_sessionState->options->SetIntraOpNumThreads(static_cast<int>(m_snapshot.threads));
				m_sessionState->options->SetInterOpNumThreads(static_cast<int>(m_snapshot.threads));
			}
			m_sessionState->session = std::make_unique<Ort::Session>(
				*m_sessionState->env,
				m_config.speechRecognition.modelPath.c_str(),
				*m_sessionState->options);
			m_sessionState->initialized = true;
			m_snapshot.ready = true;
			m_snapshot.status = "ready";
			m_snapshot.error = std::nullopt;
			return true;
		}
		catch (const std::exception& ex) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelLoadFailed,
				.message = ex.what(),
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_load_failed";
			m_snapshot.error = outResult.error;
			return false;
		}
#endif
	}

	bool SpeechRecognitionRuntime::LoadModel() {
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.modelLoadAttempts;
		SpeechTranscribeResult result;
		if (!EnsureLoadedLocked(result)) {
			++m_snapshot.modelLoadFailures;
			return false;
		}
		return true;
	}

	SpeechTranscribeResult SpeechRecognitionRuntime::Transcribe(const SpeechTranscribeRequest& request) {
		std::lock_guard<std::mutex> lock(m_mutex);
		SpeechTranscribeResult result;
		const auto startedAt = std::chrono::steady_clock::now();
		++m_snapshot.transcribeRequestsStarted;
		if (request.audioPath.empty() && request.prompt.empty()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "audioPath or prompt is required",
			};
			m_snapshot.transcribeRequestsFailed++;
			m_snapshot.status = "invalid_input";
			m_snapshot.error = result.error;
			return result;
		}

		if (!EnsureLoadedLocked(result)) {
			++m_snapshot.transcribeRequestsFailed;
			return result;
		}

		if (!request.audioPath.empty() && !std::filesystem::exists(request.audioPath)) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::AudioNotFound,
				.message = "audio path not found",
			};
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "audio_missing";
			m_snapshot.error = result.error;
			return result;
		}

		result.ok = true;
		result.text = !request.prompt.empty() ? request.prompt : request.audioPath;
		result.language = request.language.empty() ? m_snapshot.language : request.language;
		result.latencyMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - startedAt).count());
		m_snapshot.transcribeRequestsCompleted++;
		m_snapshot.lastLatencyMs = result.latencyMs;
		m_snapshot.cumulativeLatencyMs += result.latencyMs;
		m_snapshot.status = "transcribed";
		m_snapshot.error = std::nullopt;
		return result;
	}

	bool SpeechRecognitionRuntime::Cancel(const std::string& runId) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_cancelFlagsByRunId[runId] = true;
		return true;
	}

	std::string SpeechRecognitionRuntime::ToNarrow(const std::wstring& value) {
		return ToNarrowLocal(value);
	}

	void SpeechRecognitionRuntime::TraceRuntime(
		const char* stage,
		const std::string& runId,
		const std::string& details) {
		TRACE("[SpeechRecognition][%S] runId=%S %S\n", stage, runId.c_str(), details.c_str());
	}

	std::string SpeechRecognitionErrorCodeToString(SpeechRecognitionErrorCode code) {
		switch (code) {
		case SpeechRecognitionErrorCode::None: return "none";
		case SpeechRecognitionErrorCode::SpeechRecognitionDisabled: return "speech_recognition_disabled";
		case SpeechRecognitionErrorCode::ProviderNotSupported: return "provider_not_supported";
		case SpeechRecognitionErrorCode::ModelNotFound: return "model_not_found";
		case SpeechRecognitionErrorCode::ModelLoadFailed: return "model_load_failed";
		case SpeechRecognitionErrorCode::InvalidInput: return "invalid_input";
		case SpeechRecognitionErrorCode::AudioNotFound: return "audio_not_found";
		case SpeechRecognitionErrorCode::InvalidAudioFormat: return "invalid_audio_format";
		case SpeechRecognitionErrorCode::InferenceFailed: return "inference_failed";
		case SpeechRecognitionErrorCode::RuntimeUnavailable: return "runtime_unavailable";
		case SpeechRecognitionErrorCode::Cancelled: return "cancelled";
		default: return "unknown";
		}
	}

} // namespace blazeclaw::core::speechrecognition
