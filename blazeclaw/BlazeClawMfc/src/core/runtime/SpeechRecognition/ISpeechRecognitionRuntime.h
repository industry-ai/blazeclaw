#pragma once

#include "../../../config/ConfigModels.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core::speechrecognition {

	enum class SpeechRecognitionErrorCode {
		None,
		SpeechRecognitionDisabled,
		ProviderNotSupported,
		ModelNotFound,
		ModelLoadFailed,
		InvalidInput,
		AudioNotFound,
		InvalidAudioFormat,
		InferenceFailed,
		RuntimeUnavailable,
		Cancelled,
	};

	struct SpeechRecognitionError {
		SpeechRecognitionErrorCode code = SpeechRecognitionErrorCode::None;
		std::string message;
	};

	struct SpeechRecognitionRuntimeSnapshot {
		bool enabled = false;
		bool ready = false;
		bool verboseMetrics = false;
		std::string provider;
		std::string modelPath;
		std::string language;
		std::uint32_t sampleRate = 16000;
		std::uint32_t threads = 0;
		std::string executionMode;
		std::uint64_t modelLoadAttempts = 0;
		std::uint64_t modelLoadFailures = 0;
		std::uint64_t transcribeRequestsStarted = 0;
		std::uint64_t transcribeRequestsCompleted = 0;
		std::uint64_t transcribeRequestsFailed = 0;
		std::uint64_t transcribeRequestsCancelled = 0;
		std::uint64_t cumulativeLatencyMs = 0;
		std::uint32_t lastLatencyMs = 0;
		std::string status;
		std::optional<SpeechRecognitionError> error;
	};

	struct SpeechTranscribeRequest {
		std::string runId;
		std::string sessionId;
		std::string audioPath;
		std::string language;
		std::string prompt;
	};

	struct SpeechTranscribeResult {
		bool ok = false;
		bool cancelled = false;
		std::string text;
		std::string language;
		std::uint32_t latencyMs = 0;
		std::optional<SpeechRecognitionError> error;
	};

	using SpeechTranscribeCallback = std::function<void(const std::string& text)>;

	class ISpeechRecognitionRuntime {
	public:
		virtual ~ISpeechRecognitionRuntime() = default;

		virtual void Configure(const blazeclaw::config::AppConfig& appConfig) = 0;

		[[nodiscard]] virtual SpeechRecognitionRuntimeSnapshot Snapshot() const = 0;

		[[nodiscard]] virtual bool LoadModel() = 0;

		[[nodiscard]] virtual SpeechTranscribeResult Transcribe(
			const SpeechTranscribeRequest& request) = 0;

		[[nodiscard]] virtual bool Cancel(const std::string& runId) = 0;
	};

	[[nodiscard]] std::string SpeechRecognitionErrorCodeToString(
		SpeechRecognitionErrorCode code);

} // namespace blazeclaw::core::speechrecognition
