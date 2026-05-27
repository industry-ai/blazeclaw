#pragma once

#include <cstddef>
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
		StartStream,
		Streaming,
		SegmentFinalized,
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

	struct SpeechStreamingSource {
		std::string streamId;
		std::string sessionId;
		std::uint32_t sampleRate = 16000;
		std::uint32_t channels = 1;
		std::uint32_t bitsPerSample = 16;
		std::uint64_t sequenceStart = 0;
		std::uint64_t sequenceEnd = 0;
	};

	struct SpeechStreamingCursor {
		std::uint64_t startSequence = 0;
		std::uint64_t nextSequence = 0;
	};

	struct SpeechStreamingChunkPolicy {
		std::uint32_t chunkMs = 20;
		std::uint32_t overlapMs = 0;
		std::uint32_t lookbackMs = 0;
		std::size_t maxSpinCount = 64;
	};

	struct SpeechStreamingInputContract {
		SpeechStreamingSource source;
		SpeechStreamingCursor cursor;
		SpeechStreamingChunkPolicy chunkPolicy;
	};

	struct SpeechAudioArtifact {
		SpeechAudioHandoffMode handoffMode = SpeechAudioHandoffMode::WavFile;
		std::string path;
		std::string streamId;
		std::string mimeType = "audio/wav";
		std::string container = "wav";
		std::uint32_t sampleRate = 16000;
		std::uint32_t channels = 1;
		std::uint32_t bitsPerSample = 16;
		std::uint32_t frameSamples = 0;
		std::uint64_t sequenceStart = 0;
		std::uint64_t sequenceEnd = 0;
		std::uint32_t durationMs = 0;
	};

	struct SpeechTranscriptSegment {
		std::string text;
		bool final = false;
		std::uint32_t sequence = 0;
	};

	struct SpeechRecognitionDebugInfo {
		std::uint64_t sherpaChunkCount = 0;
		std::uint64_t sherpaDecodedTokenCount = 0;
		std::uint64_t sherpaEmittedTokenCount = 0;
		std::uint64_t sherpaPendingSampleCount = 0;
		std::uint64_t sherpaPartialTextLength = 0;
		std::uint64_t sherpaLoopCount = 0;
		std::uint64_t sherpaMaxLoopCount = 0;
		std::uint64_t sherpaEncoderFrameCount = 0;
		std::uint64_t sherpaJoinerCallCount = 0;
		std::uint64_t sherpaBlankTokenCount = 0;
		std::int64_t sherpaLastBestTokenId = -1;
		std::int64_t sherpaLastSecondBestTokenId = -1;
		bool sherpaSpeechActive = false;
		std::uint64_t sherpaEncoderStateCacheBindingCount = 0;
		std::uint64_t sherpaEncoderStateCacheUpdateCount = 0;
		std::uint64_t sherpaEncoderLengthOutputCount = 0;
		bool sherpaEncoderLengthOutputUsed = false;
		std::uint32_t sherpaBaselineSampleRate = 0;
		std::uint64_t sherpaBaselineChunkSamples = 0;
		std::uint64_t sherpaBaselineInputStartSequence = 0;
		std::uint64_t sherpaBaselineInputEndSequence = 0;
		std::uint64_t sherpaBaselineCursorNextSequence = 0;
		bool sherpaBaselineFinalFlush = false;
		bool sherpaBaselinePersisted = false;
		std::string sherpaBaselineExpectedText;
		std::string sherpaBaselineDecodedText;
		std::string sherpaBaselineTokenIds;
		std::string sherpaBaselineTokenPieces;
		std::string sherpaBaselineDiagnosticPath;
	};

	struct SpeechSessionState {
		std::string sessionId;
		std::string runId;
		SpeechSessionStage stage = SpeechSessionStage::Idle;
		std::string audioPath;
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::optional<SpeechStreamingInputContract> streamingInput;
		std::string transcriptText;
		std::string language;
		std::uint32_t latencyMs = 0;
		bool cancelled = false;
		std::optional<SpeechTranscriptSegment> segment;
		std::optional<SpeechRecognitionError> error;
		std::optional<SpeechRecognitionDebugInfo> debugInfo;
	};

	struct SpeechExecutionState {
		std::string sessionId;
		std::string runId;
		SpeechExecutionStage stage = SpeechExecutionStage::Queued;
		std::string audioPath;
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::optional<SpeechStreamingInputContract> streamingInput;
		std::string transcriptText;
		std::string language;
		std::string prompt;
		std::uint32_t latencyMs = 0;
		bool cancelRequested = false;
		std::optional<SpeechTranscriptSegment> segment;
		std::optional<SpeechRecognitionError> error;
	};

} // namespace blazeclaw::core::speechrecognition
