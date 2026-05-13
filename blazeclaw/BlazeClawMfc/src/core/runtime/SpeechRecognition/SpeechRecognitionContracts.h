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

	enum class SpeechExecutionStage {
		Queued,
		Recording,
		Stopped,
		Transcribing,
		Completed,
		Failed,
		Cancelled,
	};

	enum class SpeechAudioHandoffMode {
		WavFile,
		PcmStream,
	};

	struct SpeechAudioArtifact {
		SpeechAudioHandoffMode handoffMode = SpeechAudioHandoffMode::WavFile;
		std::string path;
		std::string mimeType = "audio/wav";
		std::string container = "wav";
		std::uint32_t sampleRate = 16000;
		std::uint32_t channels = 1;
		std::uint32_t bitsPerSample = 16;
		std::uint32_t durationMs = 0;
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
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::string transcriptText;
		std::string language;
		std::uint32_t latencyMs = 0;
		bool cancelled = false;
		std::optional<SpeechTranscriptSegment> segment;
		std::optional<SpeechRecognitionError> error;
	};

	struct SpeechExecutionState {
		std::string sessionId;
		std::string runId;
		SpeechExecutionStage stage = SpeechExecutionStage::Queued;
		std::string audioPath;
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::string transcriptText;
		std::string language;
		std::string prompt;
		std::uint32_t latencyMs = 0;
		bool cancelRequested = false;
		std::optional<SpeechTranscriptSegment> segment;
		std::optional<SpeechRecognitionError> error;
	};

} // namespace blazeclaw::core::speechrecognition
