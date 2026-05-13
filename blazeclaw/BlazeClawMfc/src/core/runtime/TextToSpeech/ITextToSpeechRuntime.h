#pragma once

#include "../../../config/ConfigModels.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core::texttospeech {

	enum class TextToSpeechErrorCode {
		None,
		TextToSpeechDisabled,
		ProviderNotSupported,
		ModelNotFound,
		InvalidInput,
		RuntimeUnavailable,
		Cancelled,
	};

	struct TextToSpeechError {
		TextToSpeechErrorCode code = TextToSpeechErrorCode::None;
		std::string message;
	};

	struct TextToSpeechRuntimeSnapshot {
		bool enabled = true;
		bool ready = false;
		bool speaking = false;
		std::string provider = "default";
		std::string model = "default";
		std::string voice = "default";
		std::string activeUtteranceId;
		std::uint64_t speakRequestsStarted = 0;
		std::uint64_t speakRequestsCompleted = 0;
		std::uint64_t speakRequestsFailed = 0;
		std::uint64_t stopRequests = 0;
		std::string status = "idle";
		std::optional<TextToSpeechError> error;
	};

	struct TextToSpeechSpeakRequest {
		std::string runId;
		std::string sessionId;
		std::string text;
		std::string voice;
		std::string provider;
		std::string model;
	};

	struct TextToSpeechSpeakResult {
		bool ok = false;
		bool cancelled = false;
		bool speaking = false;
		std::string utteranceId;
		std::string normalizedText;
		std::string audioPath;
		std::string voice;
		std::string provider;
		std::string model;
		std::uint32_t latencyMs = 0;
		std::string status;
		std::optional<TextToSpeechError> error;
	};

	struct TextToSpeechStopRequest {
		std::string runId;
		std::string sessionId;
		std::string utteranceId;
	};

	struct TextToSpeechStopResult {
		bool ok = false;
		bool stopped = false;
		std::string utteranceId;
		std::string status;
		std::optional<TextToSpeechError> error;
	};

	class ITextToSpeechRuntime {
	public:
		virtual ~ITextToSpeechRuntime() = default;

		virtual void Configure(const blazeclaw::config::AppConfig& appConfig) = 0;

		[[nodiscard]] virtual TextToSpeechRuntimeSnapshot Snapshot() const = 0;

		[[nodiscard]] virtual TextToSpeechSpeakResult Speak(
			const TextToSpeechSpeakRequest& request) = 0;

		[[nodiscard]] virtual TextToSpeechStopResult Stop(
			const TextToSpeechStopRequest& request) = 0;
	};

	[[nodiscard]] std::string TextToSpeechErrorCodeToString(TextToSpeechErrorCode code);

} // namespace blazeclaw::core::texttospeech
