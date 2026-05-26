#include "pch.h"
#include "SherpaZipformerStreamingEngine.h"

#include "../StreamingAudioSourceRegistry.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace blazeclaw::core::speechrecognition::engines {

	namespace {

		constexpr float kSpeechEnergyThreshold = 0.008f;

		bool IsFiniteSample(float value) {
			return std::isfinite(value) != 0;
		}

		float ComputeFrameEnergy(const std::vector<float>& samples) {
			if (samples.empty()) {
				return 0.0f;
			}

			double sumSquares = 0.0;
			for (const auto sample : samples) {
				if (!IsFiniteSample(sample)) {
					continue;
				}
				sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
			}
			const double meanSquare = sumSquares / static_cast<double>(samples.size());
			return static_cast<float>(std::sqrt((std::max)(0.0, meanSquare)));
		}

	} // namespace

	bool SherpaZipformerStreamingEngine::Load(
		const std::filesystem::path& rootPath,
		const SpeechModelLayoutProbeResult& layout,
		std::string& outError) {
		outError.clear();
		m_loaded = false;
		m_artifacts = {};

		if (layout.kind != SpeechModelLayoutKind::SherpaZipformerTransducer) {
			outError = "sherpa layout not detected";
			return false;
		}

		m_artifacts.encoderPath = layout.sherpaEncoderPath;
		m_artifacts.decoderPath = layout.sherpaDecoderPath;
		m_artifacts.joinerPath = layout.sherpaJoinerPath;
		m_artifacts.tokensPath = layout.sherpaTokensPath;

		if (!std::filesystem::exists(m_artifacts.encoderPath) ||
			!std::filesystem::exists(m_artifacts.decoderPath) ||
			!std::filesystem::exists(m_artifacts.joinerPath) ||
			!std::filesystem::exists(m_artifacts.tokensPath)) {
			outError = "required sherpa artifacts are missing";
			return false;
		}

		m_artifacts.tokenCount = CountTokens(m_artifacts.tokensPath);
		if (m_artifacts.tokenCount == 0) {
			outError = "tokens file is empty";
			return false;
		}

		UNREFERENCED_PARAMETER(rootPath);
		m_loaded = true;
		return true;
	}

	bool SherpaZipformerStreamingEngine::IsLoaded() const {
		return m_loaded;
	}

	const SherpaZipformerStreamingEngine::LoadedArtifacts&
	SherpaZipformerStreamingEngine::Artifacts() const {
		return m_artifacts;
	}

	SpeechTranscribeResult SherpaZipformerStreamingEngine::TranscribeStreaming(
		const SpeechTranscribeRequest& request,
		const std::function<bool(const std::string&)>& isCancelled) const {
		SpeechTranscribeResult result;
		result.sessionState.sessionId = request.sessionId;
		result.sessionState.runId = request.runId;
		result.sessionState.audioPath = request.audioPath;
		result.sessionState.audioArtifact = request.audioArtifact;
		result.sessionState.streamingInput = request.streamingInput;
		result.sessionState.language = request.language.empty() ? "und" : request.language;
		result.sessionState.stage = SpeechSessionStage::Transcribing;

		if (!m_loaded) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
				.message = "sherpa streaming engine is not loaded",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		if (!request.streamingInput.has_value()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "streamingInput is required for sherpa streaming transcription",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		const auto& streamingInput = *request.streamingInput;
		if (streamingInput.source.streamId.empty()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "streamingInput.source.streamId is required",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		const std::uint32_t sampleRate =
			streamingInput.source.sampleRate == 0
			? 16000U
			: streamingInput.source.sampleRate;
		std::uint32_t chunkMs = streamingInput.chunkPolicy.chunkMs;
		if (chunkMs == 0) {
			chunkMs = 20;
		}

		const std::uint64_t chunkSamplesRaw =
			(static_cast<std::uint64_t>(sampleRate) * chunkMs) / 1000ULL;
		const std::size_t chunkSamples = static_cast<std::size_t>((std::max)(
			std::uint64_t{ 1 },
			chunkSamplesRaw));

		std::uint64_t nextSequence =
			streamingInput.cursor.nextSequence > 0
			? streamingInput.cursor.nextSequence
			: streamingInput.source.sequenceStart;

		const auto oldestOpt =
			GetStreamingAudioOldestSequence(streamingInput.source.streamId);
		if (oldestOpt.has_value() && nextSequence < *oldestOpt) {
			nextSequence = *oldestOpt;
		}

		std::uint64_t loopGuard = 0;
		std::vector<float> chunk;
		bool speechActive = false;
		bool speechDetected = false;
		std::uint64_t speechStart = 0;
		std::uint64_t speechEnd = 0;
		const std::uint64_t maxLoops = 4096;

		for (;;) {
			if (isCancelled && isCancelled(request.runId)) {
				result.ok = false;
				result.cancelled = true;
				result.error = SpeechRecognitionError{
					.code = SpeechRecognitionErrorCode::Cancelled,
					.message = "transcription cancelled",
				};
				result.sessionState.cancelled = true;
				result.sessionState.stage = SpeechSessionStage::Failed;
				result.sessionState.error = result.error;
				return result;
			}

			if (++loopGuard > maxLoops) {
				break;
			}

			const auto latestOpt =
				GetStreamingAudioLatestSequence(streamingInput.source.streamId);
			if (!latestOpt.has_value() || nextSequence >= *latestOpt) {
				break;
			}

			const std::size_t requestSamples = static_cast<std::size_t>((std::min)(
				static_cast<std::uint64_t>(chunkSamples),
				*latestOpt - nextSequence));
			if (requestSamples == 0) {
				break;
			}

			if (!ReadStreamingAudioBySequence(
				streamingInput.source.streamId,
				nextSequence,
				requestSamples,
				chunk)) {
				break;
			}

			const float energy = ComputeFrameEnergy(chunk);
			const bool frameSpeech = energy >= kSpeechEnergyThreshold;
			if (frameSpeech) {
				if (!speechActive) {
					speechActive = true;
					speechStart = nextSequence;
				}
				speechEnd = nextSequence + static_cast<std::uint64_t>(requestSamples);
				speechDetected = true;
			} else if (speechActive) {
				speechActive = false;
			}

			nextSequence += static_cast<std::uint64_t>(requestSamples);
		}

		result.ok = true;
		result.cancelled = false;
		result.language = result.sessionState.language;
		result.sessionState.stage = SpeechSessionStage::Completed;
		result.sessionState.error = std::nullopt;

		if (speechDetected) {
			result.text = "[sherpa-streaming] speech detected";
			result.sessionState.transcriptText = result.text;
			SpeechTranscriptSegment segment;
			segment.text = result.text;
			segment.final = true;
			segment.sequence = 1;
			result.sessionState.segment = segment;
			if (sampleRate > 0 && speechEnd > speechStart) {
				const std::uint64_t latencyMs =
					((speechEnd - speechStart) * 1000ULL) /
					static_cast<std::uint64_t>(sampleRate);
				result.latencyMs = static_cast<std::uint32_t>(latencyMs);
				result.sessionState.latencyMs = result.latencyMs;
			}
		}

		return result;
	}

	std::size_t SherpaZipformerStreamingEngine::CountTokens(
		const std::filesystem::path& tokensPath) {
		std::ifstream stream(tokensPath);
		if (!stream.is_open()) {
			return 0;
		}

		std::size_t count = 0;
		std::string line;
		while (std::getline(stream, line)) {
			if (!line.empty()) {
				++count;
			}
		}
		return count;
	}

} // namespace blazeclaw::core::speechrecognition::engines
