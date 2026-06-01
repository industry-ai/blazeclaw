#pragma once

#include "../ISpeechRecognitionRuntime.h"
#include "../SpeechModelLayoutProbe.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

namespace blazeclaw::core::speechrecognition::engines {

	struct SherpaOnlineFbankFrontend;

	class SherpaZipformerStreamingEngine {
	public:
		enum class TensorBindingKind {
			Features,
			FeatureLengths,
			EncoderOut,
			EncoderOutLengths,
			EncoderState,
			ProcessedLengths,
			DecoderInputTokens,
			DecoderOut,
			JoinerLogits,
			UnknownInt64,
			UnknownFloat,
		};

		struct TensorBinding {
			std::string name;
			std::string normalizedStateName;
			TensorBindingKind kind = TensorBindingKind::UnknownFloat;
			std::vector<std::int64_t> shape;
			std::size_t ordinal = 0;
			bool hasDynamicShape = false;
			bool isStateTensor = false;
			bool isLengthLike = false;
#if BLAZECLAW_HAS_ONNXRUNTIME
			ONNXTensorElementDataType elementType =
				ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED;
#endif
			std::size_t bufferIndex = 0;
		};

		struct EncoderStateCacheBinding {
			std::size_t inputBindingIndex = 0;
			std::size_t outputBindingIndex = 0;
			std::size_t cacheIndex = 0;
			std::string normalizedStateName;
			std::vector<std::int64_t> inputShape;
			std::vector<std::int64_t> outputShape;
			std::uint64_t inputStaticElementCount = 0;
			std::uint64_t outputStaticElementCount = 0;
			bool inputHasDynamicShape = false;
			bool outputHasDynamicShape = false;
			bool isInt64 = false;
		};

		struct LoadedArtifacts {
			std::filesystem::path encoderPath;
			std::filesystem::path decoderPath;
			std::filesystem::path joinerPath;
			std::filesystem::path tokensPath;
			std::filesystem::path bpeModelPath;
			std::filesystem::path bpeVocabPath;
			std::size_t tokenCount = 0;
			bool bpeModelPresent = false;
			bool bpeVocabPresent = false;
		};

		struct ExecutionProviderOptions {
			bool cudaEnabled = false;
			std::uint32_t threads = 0;
			std::string executionMode = "sequential";
			bool cudaCompatibilityGuardLatched = false;
			std::string cudaCompatibilityGuardLatchedReason;
		};

		struct ExecutionProviderStatus {
			bool cudaExecutionProviderAvailable = false;
			bool cudaExecutionProviderEnabled = false;
			std::string cudaExecutionProviderReason;
			std::string effectiveExecutionProvider = "cpu";
			bool cudaCompatibilityGuardLatched = false;
			std::string cudaCompatibilityGuardLatchedReason;
		};

		struct StreamState {
			std::chrono::steady_clock::time_point firstTokenTraceStart{};
			std::uint64_t firstTokenFirstAudioReadableOffsetMs = 0;
			std::uint64_t firstTokenFirstAudioAcceptedOffsetMs = 0;
			std::uint64_t firstTokenEncoderStartOffsetMs = 0;
			std::uint64_t firstTokenEncoderEndOffsetMs = 0;
			std::uint64_t firstTokenDecoderStartOffsetMs = 0;
			std::uint64_t firstTokenJoinerStartOffsetMs = 0;
			std::uint64_t firstTokenPartialTextOffsetMs = 0;
			std::uint64_t nextSequence = 0;
			std::uint32_t segmentSequence = 0;
			std::vector<std::int64_t> decoderContext;
			std::vector<std::int64_t> emittedTokenIds;
			std::vector<std::int64_t> baselineTokenIds;
			std::shared_ptr<SherpaOnlineFbankFrontend> onlineFbank;
			std::vector<float> pendingFeatureFrames;
			std::size_t pendingFeatureFrameCount = 0;
			std::vector<std::vector<std::int64_t>> encoderInt64StateCaches;
			std::vector<std::vector<float>> encoderFloatStateCaches;
			std::vector<std::vector<std::int64_t>> encoderInt64StateCacheShapes;
			std::vector<std::vector<std::int64_t>> encoderFloatStateCacheShapes;
			std::uint64_t chunkCount = 0;
			double chunkEnergySum = 0.0;
			float chunkEnergyMin = 0.0f;
			float chunkEnergyMax = 0.0f;
			bool chunkEnergyObserved = false;
			std::uint64_t voicedChunkCount = 0;
			std::uint64_t voicedChunkStreak = 0;
			float adaptiveSpeechEnergyFloor = 0.0f;
			std::uint64_t nearZeroChunkCount = 0;
			std::uint64_t nearZeroSampleCount = 0;
			std::uint64_t totalSampleCount = 0;
			std::uint64_t requestedSequenceStart = 0;
			std::uint64_t decodedTokenCount = 0;
			std::uint64_t encoderFrameCount = 0;
			std::uint64_t joinerCallCount = 0;
			std::uint64_t blankTokenCount = 0;
			std::uint64_t encoderStateCacheUpdateCount = 0;
			std::uint64_t encoderStateCacheValidatedUpdateCount = 0;
			std::uint64_t encoderStateCacheContractFailureCount = 0;
			std::uint64_t encoderLengthOutputCount = 0;
			std::uint64_t contractFeatureFrameCount = 0;
			std::uint64_t contractFeatureRealFrameCount = 0;
			std::uint64_t contractFeaturePaddedFrameCount = 0;
			std::uint64_t contractEncoderValidFrameCount = 0;
			std::string contractFbankSampleScalingMode;
			std::string contractFeatureInputShape;
			std::string contractFeatureLengthValue;
			std::string contractFeatureFirstFrameStats;
			std::string contractFeatureLastFrameStats;
			std::string contractEncoderOutputShape;
			std::string contractDecoderInputContext;
			std::string contractDecoderInputShape;
			std::string contractDecoderOutputShape;
			std::string contractDecoderVectorSlice;
			std::string contractJoinerEncoderInputShape;
			std::string contractJoinerDecoderInputShape;
			std::string contractJoinerOutputShape;
			std::string contractJoinerLogitsSlice;
			std::string contractJoinerTopTokens;
			std::string contractDecoderJoinerSummary;
			std::string contractDecoderJoinerLastError;
			std::string contractStateCacheSummary;
			std::string contractStateCacheLastError;
			std::uint64_t decoderJoinerContractFailureCount = 0;
			std::uint64_t decoderJoinerValidatedCallCount = 0;
			std::uint64_t rnntInnerLoopCount = 0;
			std::uint64_t rnntMaxSymbolsHitCount = 0;
			std::uint64_t rnntRepeatedTokenCount = 0;
			std::uint64_t rnntMultiSymbolFrameCount = 0;
			std::uint64_t rnntMaxSymbolsPerFrame = 0;
			std::uint64_t rnntAdaptiveFrameStopCount = 0;
			std::string rnntLastFrameStopReason;
			std::uint64_t repeatedTokenNgramLength = 0;
			std::uint64_t repeatedTokenNgramCount = 0;
			std::string repeatedTokenNgramUnit;
			std::string repeatedDecodedUnit;
			std::uint64_t repeatedDecodedUnitLength = 0;
			std::uint64_t repeatedDecodedUnitCount = 0;
			double repeatedDecodedUnitCoverage = 0.0;
			bool decodedRepeatFinalRejected = false;
			std::string repeatGuardAction;
			bool encoderLengthOutputUsed = false;
			std::int64_t lastBestTokenId = -1;
			float lastBestTokenScore = 0.0f;
			std::int64_t lastSecondBestTokenId = -1;
			float lastSecondBestTokenScore = 0.0f;
			std::uint32_t silenceChunkCount = 0;
			bool speechActive = false;
			std::string partialText;
		};

		bool Load(
			const std::filesystem::path& rootPath,
			const SpeechModelLayoutProbeResult& layout,
			std::string& outError);
		bool Load(
			const std::filesystem::path& rootPath,
			const SpeechModelLayoutProbeResult& layout,
			const ExecutionProviderOptions& providerOptions,
			ExecutionProviderStatus& providerStatus,
			std::string& outError);

		[[nodiscard]] bool IsLoaded() const;
		[[nodiscard]] const LoadedArtifacts& Artifacts() const;

		SpeechTranscribeResult TranscribeStreaming(
			const SpeechTranscribeRequest& request,
			const std::function<bool(const std::string&)>& isCancelled) const;

	private:
		[[nodiscard]] static std::size_t CountTokens(
			const std::filesystem::path& tokensPath);
		[[nodiscard]] static TensorBindingKind ClassifyBindingKind(
			const std::string& name,
			bool isEncoder,
			bool isJoinerOutput = false);
		[[nodiscard]] static std::string DecodeTokenPiece(
			const std::string& piece);
		[[nodiscard]] static bool IsSpecialTokenPiece(
			const std::string& piece);
		[[nodiscard]] static std::string NormalizeDecodedBpeText(
			std::string text);
		[[nodiscard]] static std::string EscapeJsonString(
			const std::string& value);
		[[nodiscard]] static std::string JoinTokenIds(
			const std::vector<std::int64_t>& tokenIds);
		[[nodiscard]] std::string JoinTokenPieces(
			const std::vector<std::int64_t>& tokenIds) const;
		[[nodiscard]] std::string DecodeTokenIdsToText(
			const std::vector<std::int64_t>& tokenIds) const;
		[[nodiscard]] static std::optional<std::filesystem::path> ResolveBaselineDiagnosticsDirectory();
		[[nodiscard]] static std::string ResolveBaselineExpectedText();
		[[nodiscard]] static bool IsBaselinePersistenceEnabled();
		[[nodiscard]] std::optional<std::filesystem::path> PersistBaselineDiagnostics(
			const SpeechTranscribeRequest& request,
			const SpeechStreamingInputContract& streamingInput,
			const StreamState& streamState,
			std::uint32_t sampleRate,
			std::size_t chunkSamples,
			std::uint64_t loopGuard,
			std::uint64_t maxLoops,
			bool finalFlush,
			const std::string& expectedText,
			const std::string& decodedText,
			const std::string& finalOutcome,
			bool hasSegment,
			bool fallbackUsed) const;
		void ClearStreamState(
			const std::string& streamId) const;

		mutable std::mutex m_streamMutex;
		mutable std::unordered_map<std::string, StreamState> m_streamStateByStreamId;
		std::unordered_map<std::int64_t, std::string> m_tokenById;
		std::int64_t m_blankId = 0;
		std::int64_t m_unkId = 2;
		std::int64_t m_eosId = 1;
		std::string m_effectiveExecutionProvider = "cpu";
		std::string m_cudaExecutionProviderReason;

#if BLAZECLAW_HAS_ONNXRUNTIME
		std::unique_ptr<Ort::Env> m_env;
		std::unique_ptr<Ort::SessionOptions> m_options;
		std::unique_ptr<Ort::Session> m_encoderSession;
		std::unique_ptr<Ort::Session> m_decoderSession;
		std::unique_ptr<Ort::Session> m_joinerSession;
		std::vector<TensorBinding> m_encoderInputBindings;
		std::vector<TensorBinding> m_encoderOutputBindings;
		std::vector<EncoderStateCacheBinding> m_encoderStateCacheBindings;
		std::vector<std::string> m_encoderOutputNames;
		std::vector<TensorBinding> m_decoderInputBindings;
		std::vector<std::string> m_decoderOutputNames;
		std::vector<TensorBinding> m_joinerInputBindings;
		std::vector<std::string> m_joinerOutputNames;
		std::size_t m_encoderMainOutputIndex = 0;
		std::size_t m_decoderMainOutputIndex = 0;
		std::size_t m_joinerMainOutputIndex = 0;
		std::size_t m_decoderContextSize = 2;
#endif

		LoadedArtifacts m_artifacts;
		bool m_loaded = false;
	};

} // namespace blazeclaw::core::speechrecognition::engines
