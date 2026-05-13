#pragma once

#include <cstdint>
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
		AudioDecodeFailed,
		FeatureExtractionFailed,
		TokenizerLoadFailed,
		DecoderFailed,
		InferenceFailed,
		RuntimeUnavailable,
		Cancelled,
	};

	struct SpeechRecognitionError {
		SpeechRecognitionErrorCode code = SpeechRecognitionErrorCode::None;
		std::string message;
	};

	enum class SpeechSessionStage {
		Idle,
		Recording,
		Paused,
		Stopped,
		Transcribing,
		Completed,
		Failed,
	};

	struct SpeechTranscriptSegment {
		std::string text;
		bool final = false;
		std::uint32_t sequence = 0;
	};

	struct SpeechSessionState {
		std::string sessionId;
		std::string runId;
		SpeechSessionStage stage = SpeechSessionStage::Idle;
		std::string audioPath;
		std::string transcriptText;
		std::string language;
		std::uint32_t latencyMs = 0;
		bool cancelled = false;
		std::optional<SpeechTranscriptSegment> segment;
		std::optional<SpeechRecognitionError> error;
	};

} // namespace blazeclaw::core::speechrecognition
