#pragma once

#include "SpeechRecognitionContracts.h"
#include "../../../config/ConfigModels.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core::speechrecognition {

	struct SpeechRecognitionRuntimeSnapshot {
		bool enabled = false;
		bool ready = false;
		bool verboseMetrics = false;
		std::string provider;
		std::string modelPath;
		std::string runtimeHotMode;
		std::string runtimeHotLifecycleState;
		bool runtimeHotWarmupEnabled = false;
		std::uint32_t runtimeHotWarmupRuns = 0;
		std::uint32_t runtimeHotIdleTimeoutMs = 0;
		bool hotwordsEnabled = false;
		std::uint32_t hotwordsCount = 0;
		std::uint32_t hotwordsMaxCount = 0;
		std::string hotwordsApplyStage;
		bool hotwordsDebugDumpPrompt = false;
		std::string lastPromptBuildStatus;
		std::string lastPromptBuildError;
		std::string language;
		std::uint32_t sampleRate = 16000;
		bool streamingEnabled = false;
		std::uint32_t streamingChunkMs = 0;
		std::uint32_t streamingLookbackMs = 0;
		std::uint32_t threads = 0;
		std::string executionMode;
		bool cudaExecutionProviderAvailable = false;
		bool cudaExecutionProviderEnabled = false;
		std::string cudaExecutionProviderReason;
		std::string effectiveExecutionProvider;
		std::uint64_t modelLoadAttempts = 0;
		std::uint64_t modelLoadFailures = 0;
		std::uint64_t transcribeRequestsStarted = 0;
		std::uint64_t transcribeRequestsCompleted = 0;
		std::uint64_t transcribeRequestsFailed = 0;
		std::uint64_t transcribeRequestsCancelled = 0;
		std::uint64_t cumulativeLatencyMs = 0;
		std::uint32_t lastLatencyMs = 0;
		std::uint32_t lastPreprocessLatencyMs = 0;
		std::uint32_t lastInferenceLatencyMs = 0;
		std::uint32_t lastDecodeLatencyMs = 0;
		std::uint32_t lastAudioDurationMs = 0;
		std::uint32_t lastInputSampleRate = 0;
		std::uint32_t lastInputChannels = 0;
		bool lastInputResampled = false;
		std::uint32_t lastFeatureFrames = 0;
		std::uint32_t lastFeatureBins = 0;
		std::string modelLayout;
		std::string modelVariant;
		std::string encoderModelPath;
		std::string decoderInitModelPath;
		std::string decoderStepModelPath;
		std::string tokenizerPath;
		std::string status;
		std::optional<SpeechRecognitionError> error;
	};

	struct SpeechExecutionRequest {
		std::string runId;
		std::string sessionId;
		std::string audioPath;
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::optional<SpeechStreamingInputContract> streamingInput;
		std::string language;
		std::string prompt;
	};

	struct SpeechExecutionAccepted {
		bool accepted = false;
		SpeechExecutionState executionState;
		std::optional<SpeechRecognitionError> error;
	};

	struct SpeechExecutionStatus {
		bool found = false;
		SpeechExecutionState executionState;
	};

	struct SpeechTranscribeRequest {
		std::string runId;
		std::string sessionId;
		std::string audioPath;
		std::optional<SpeechAudioArtifact> audioArtifact;
		std::optional<SpeechStreamingInputContract> streamingInput;
		std::string language;
		std::string prompt;
	};

	struct SpeechTranscribeResult {
		bool ok = false;
		bool cancelled = false;
		std::string text;
		std::string language;
		std::uint32_t latencyMs = 0;
		SpeechSessionState sessionState;
		std::optional<SpeechRecognitionError> error;
	};

	using SpeechExecutionUpdateCallback = std::function<void(
		const SpeechExecutionState& state)>;

	using SpeechTranscribeCallback = std::function<void(
		const SpeechTranscribeResult& result)>;

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
