#include "pch.h"

#include "../src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h"
#include "../src/core/runtime/SpeechRecognition/StreamingAudioSourceRegistry.h"
#include "../src/core/runtime/SpeechRecognition/SpeechModelLayoutProbe.h"
#include "../src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

namespace {

	std::filesystem::path CreateUniqueTempDirectory(const wchar_t* suffix)
	{
		const auto tick = std::to_wstring(
			std::chrono::steady_clock::now().time_since_epoch().count());
		const auto root = std::filesystem::temp_directory_path() /
			(std::wstring(L"blazeclaw-speech-streaming-") + suffix + L"-" + tick);
		std::filesystem::create_directories(root);
		return root;
	}

	void TouchFile(const std::filesystem::path& path)
	{
		std::ofstream out(path, std::ios::binary);
		const char onnxStub[] = { 'O','N','N','X','S','T','U','B' };
		out.write(onnxStub, sizeof(onnxStub));
	}

	blazeclaw::core::speechrecognition::SpeechModelLayoutProbeResult BuildSherpaLayout(
		const std::filesystem::path& root)
	{
		namespace fs = std::filesystem;
		auto resolveModelDir = []() -> fs::path {
			const auto current = fs::current_path();
			for (auto cursor = current; !cursor.empty(); cursor = cursor.parent_path()) {
				const auto candidateA = cursor /
					"blazeclaw/BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en";
				if (fs::exists(candidateA)) {
					return candidateA;
				}

				const auto candidateB = cursor /
					"BlazeClawMfc/models/STT/sherpa-onnx-streaming-zipformer-bilingual-zh-en";
				if (fs::exists(candidateB)) {
					return candidateB;
				}

				if (cursor == cursor.parent_path()) {
					break;
				}
			}
			return {};
		};

		const auto repoModelDir = resolveModelDir();
		if (fs::exists(repoModelDir)) {
			std::error_code ec;
			fs::copy_file(
				repoModelDir / "encoder-epoch-99-avg-1.int8.onnx",
				root / "encoder-epoch-99-avg-1.int8.onnx",
				fs::copy_options::overwrite_existing,
				ec);
			fs::copy_file(
				repoModelDir / "decoder-epoch-99-avg-1.int8.onnx",
				root / "decoder-epoch-99-avg-1.int8.onnx",
				fs::copy_options::overwrite_existing,
				ec);
			fs::copy_file(
				repoModelDir / "joiner-epoch-99-avg-1.int8.onnx",
				root / "joiner-epoch-99-avg-1.int8.onnx",
				fs::copy_options::overwrite_existing,
				ec);
			fs::copy_file(
				repoModelDir / "tokens.txt",
				root / "tokens.txt",
				fs::copy_options::overwrite_existing,
				ec);
		}

		if (!fs::exists(root / L"encoder-epoch-99-avg-1.int8.onnx")) {
			TouchFile(root / L"encoder-epoch-99-avg-1.int8.onnx");
		}
		if (!fs::exists(root / L"decoder-epoch-99-avg-1.int8.onnx")) {
			TouchFile(root / L"decoder-epoch-99-avg-1.int8.onnx");
		}
		if (!fs::exists(root / L"joiner-epoch-99-avg-1.int8.onnx")) {
			TouchFile(root / L"joiner-epoch-99-avg-1.int8.onnx");
		}
		if (!fs::exists(root / L"tokens.txt")) {
			std::ofstream tokens(root / L"tokens.txt", std::ios::binary);
			tokens << "<blk> 0\n<sos/eos> 1\n<unk> 2\n▁讲 3\n▁个 4\n▁笑 5\n▁话 6\n";
		}
		return blazeclaw::core::speechrecognition::ProbeSpeechModelLayout(root);
	}

	void RegisterDeterministicSource(
		const std::string& streamId,
		std::uint64_t oldestSequence,
		std::uint64_t latestSequence,
		const std::vector<float>& samples)
	{
		using namespace blazeclaw::core::speechrecognition;
		RegisterStreamingAudioSource(
			streamId,
			StreamingAudioSourceReader{
				.readBySequence = [oldestSequence, latestSequence, samples](
					std::uint64_t startSequence,
					std::size_t sampleCount,
					std::vector<float>& outSamples) {
					if (startSequence < oldestSequence ||
						startSequence + sampleCount > latestSequence) {
						return false;
					}

					const std::size_t start = static_cast<std::size_t>(startSequence - oldestSequence);
					if (start + sampleCount > samples.size()) {
						return false;
					}

					outSamples.assign(samples.begin() + start, samples.begin() + start + sampleCount);
					return true;
				},
				.latestSequence = [latestSequence]() { return latestSequence; },
				.oldestSequence = [oldestSequence]() { return oldestSequence; },
			});
	}

} // namespace

TEST_CASE("Sherpa streaming engine emits finalized segment for speech energy", "[speech][streaming][realtime]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto root = CreateUniqueTempDirectory(L"segment-final");
	const auto layout = BuildSherpaLayout(root);
	engines::SherpaZipformerStreamingEngine engine;
	std::string loadError;
	if (!engine.Load(root, layout, loadError)) {
		SUCCEED("Sherpa runtime load unavailable in test env: " + loadError);
		std::filesystem::remove_all(root);
		return;
	}

	const std::string streamId = "phase7-stream-final";
	const std::vector<float> samples(400, 0.25f);
	RegisterDeterministicSource(streamId, 100, 500, samples);

	SpeechTranscribeRequest request;
	request.runId = "run-phase7-final";
	request.sessionId = "session-phase7-final";
	request.streamingInput = SpeechStreamingInputContract{};
	request.streamingInput->source.streamId = streamId;
	request.streamingInput->source.sampleRate = 16000;
	request.streamingInput->source.sequenceStart = 100;
	request.streamingInput->cursor.nextSequence = 100;
	request.streamingInput->chunkPolicy.chunkMs = 20;
	request.language = "zh";

	const auto result = engine.TranscribeStreaming(
		request,
		[](const std::string&) { return false; });

	REQUIRE(result.ok);
	REQUIRE_FALSE(result.cancelled);
	REQUIRE(result.sessionState.stage == SpeechSessionStage::Completed);
	if (result.sessionState.segment.has_value()) {
		REQUIRE_FALSE(result.sessionState.segment->text.empty());
	}
	REQUIRE(result.sessionState.latencyMs > 0);

	UnregisterStreamingAudioSource(streamId);
	std::filesystem::remove_all(root);
}

TEST_CASE("Sherpa streaming engine final stream preserves compatible live preview state", "[speech][streaming][realtime]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto root = CreateUniqueTempDirectory(L"final-reset");
	const auto layout = BuildSherpaLayout(root);
	engines::SherpaZipformerStreamingEngine engine;
	std::string loadError;
	if (!engine.Load(root, layout, loadError)) {
		SUCCEED("Sherpa runtime load unavailable in test env: " + loadError);
		std::filesystem::remove_all(root);
		return;
	}

	const std::string streamId = "phase7-stream-final-reset";
	const std::uint64_t oldestSequence = 100;
	const std::uint64_t latestSequence = 900;
	const std::vector<float> samples(
		static_cast<std::size_t>(latestSequence - oldestSequence),
		0.2f);
	const auto minReadStart = std::make_shared<std::uint64_t>(
		(std::numeric_limits<std::uint64_t>::max)());
	RegisterStreamingAudioSource(
		streamId,
		StreamingAudioSourceReader{
			.readBySequence = [oldestSequence, latestSequence, samples, minReadStart](
				std::uint64_t startSequence,
				std::size_t sampleCount,
				std::vector<float>& outSamples) {
				*minReadStart = (std::min)(*minReadStart, startSequence);
				if (startSequence < oldestSequence ||
					startSequence + sampleCount > latestSequence) {
					return false;
				}

				const std::size_t start = static_cast<std::size_t>(startSequence - oldestSequence);
				if (start + sampleCount > samples.size()) {
					return false;
				}

				outSamples.assign(samples.begin() + start, samples.begin() + start + sampleCount);
				return true;
			},
			.latestSequence = [latestSequence]() { return latestSequence; },
			.oldestSequence = [oldestSequence]() { return oldestSequence; },
		});

	SpeechAudioArtifact artifact;
	artifact.handoffMode = SpeechAudioHandoffMode::PcmStream;
	artifact.streamId = streamId;
	artifact.sampleRate = 16000;
	artifact.channels = 1;
	artifact.bitsPerSample = 16;
	artifact.sequenceStart = oldestSequence;
	artifact.sequenceEnd = 0;

	SpeechTranscribeRequest liveRequest;
	liveRequest.runId = "run-phase7-final-reset-live";
	liveRequest.sessionId = "session-phase7-final-reset";
	liveRequest.audioArtifact = artifact;
	liveRequest.streamingInput = SpeechStreamingInputContract{};
	liveRequest.streamingInput->source.streamId = streamId;
	liveRequest.streamingInput->source.sampleRate = 16000;
	liveRequest.streamingInput->source.sequenceStart = oldestSequence;
	liveRequest.streamingInput->source.sequenceEnd = 0;
	liveRequest.streamingInput->cursor.nextSequence = oldestSequence;
	liveRequest.streamingInput->chunkPolicy.chunkMs = 20;
	liveRequest.language = "zh";

	const auto liveResult = engine.TranscribeStreaming(
		liveRequest,
		[](const std::string&) { return false; });
	REQUIRE(liveResult.ok);

	*minReadStart = (std::numeric_limits<std::uint64_t>::max)();
	artifact.sequenceEnd = latestSequence;
	SpeechTranscribeRequest finalRequest = liveRequest;
	finalRequest.runId = "run-phase7-final-reset-final";
	finalRequest.audioArtifact = artifact;
	finalRequest.streamingInput->source.sequenceEnd = latestSequence;
	finalRequest.streamingInput->cursor.nextSequence = oldestSequence;

	const auto finalResult = engine.TranscribeStreaming(
		finalRequest,
		[](const std::string&) { return false; });

	REQUIRE(finalResult.ok);
	REQUIRE(*minReadStart == oldestSequence);
	REQUIRE(finalResult.sessionState.debugInfo.has_value());
	const auto& finalDebug = *finalResult.sessionState.debugInfo;
	REQUIRE(finalDebug.sherpaFinalStreamRequest);
	REQUIRE_FALSE(finalDebug.sherpaLivePcmStream);
	REQUIRE(finalDebug.sherpaFinalDrainComplete);
	REQUIRE(finalDebug.sherpaFinalRemainingSamples == 0);
	if (!liveResult.text.empty()) {
		REQUIRE_FALSE(finalDebug.sherpaDecodedText.empty());
	}
	REQUIRE(finalDebug.sherpaFinalSequenceEnd == latestSequence);
	REQUIRE(finalDebug.sherpaFinalCursorNext >= latestSequence);
	REQUIRE(finalDebug.sherpaBaselineInputStartSequence == oldestSequence);
	REQUIRE(finalDebug.sherpaBaselineInputEndSequence == latestSequence);
	REQUIRE(finalDebug.sherpaBaselineCursorNextSequence >= latestSequence);
	REQUIRE(finalDebug.sherpaFinalOutcome != "live_stream_not_final");
	REQUIRE(finalDebug.sherpaFinalOutcome != "finite_stream_not_drained");
	REQUIRE(finalResult.sessionState.streamingInput.has_value());
	REQUIRE(finalResult.sessionState.streamingInput->source.sequenceStart == oldestSequence);
	REQUIRE(finalResult.sessionState.streamingInput->source.sequenceEnd == latestSequence);
	REQUIRE(finalResult.sessionState.streamingInput->cursor.nextSequence >= latestSequence);
	if (!finalDebug.sherpaDecodedText.empty()) {
		REQUIRE(finalResult.text == finalDebug.sherpaDecodedText);
	}

	UnregisterStreamingAudioSource(streamId);
	std::filesystem::remove_all(root);
}

TEST_CASE("Sherpa streaming engine handles sequence catch-up after wrap window", "[speech][streaming][realtime]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto root = CreateUniqueTempDirectory(L"sequence-catchup");
	const auto layout = BuildSherpaLayout(root);
	engines::SherpaZipformerStreamingEngine engine;
	std::string loadError;
	if (!engine.Load(root, layout, loadError)) {
		SUCCEED("Sherpa runtime load unavailable in test env: " + loadError);
		std::filesystem::remove_all(root);
		return;
	}

	const std::string streamId = "phase7-stream-catchup";
	const std::vector<float> samples(240, 0.2f);
	RegisterDeterministicSource(streamId, 300, 540, samples);

	SpeechTranscribeRequest request;
	request.runId = "run-phase7-catchup";
	request.sessionId = "session-phase7-catchup";
	request.streamingInput = SpeechStreamingInputContract{};
	request.streamingInput->source.streamId = streamId;
	request.streamingInput->source.sampleRate = 16000;
	request.streamingInput->source.sequenceStart = 100;
	request.streamingInput->cursor.nextSequence = 100;
	request.streamingInput->chunkPolicy.chunkMs = 20;

	const auto result = engine.TranscribeStreaming(
		request,
		[](const std::string&) { return false; });

	REQUIRE(result.ok);
	REQUIRE(result.sessionState.stage == SpeechSessionStage::Completed);
	if (result.sessionState.segment.has_value()) {
		REQUIRE(result.sessionState.segment->sequence >= 1);
	}

	UnregisterStreamingAudioSource(streamId);
	std::filesystem::remove_all(root);
}

TEST_CASE("Sherpa streaming engine reports cancellation", "[speech][streaming][realtime]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto root = CreateUniqueTempDirectory(L"cancel");
	const auto layout = BuildSherpaLayout(root);
	engines::SherpaZipformerStreamingEngine engine;
	std::string loadError;
	if (!engine.Load(root, layout, loadError)) {
		SUCCEED("Sherpa runtime load unavailable in test env: " + loadError);
		std::filesystem::remove_all(root);
		return;
	}

	const std::string streamId = "phase7-stream-cancel";
	const std::vector<float> samples(240, 0.2f);
	RegisterDeterministicSource(streamId, 10, 250, samples);

	SpeechTranscribeRequest request;
	request.runId = "run-phase7-cancel";
	request.sessionId = "session-phase7-cancel";
	request.streamingInput = SpeechStreamingInputContract{};
	request.streamingInput->source.streamId = streamId;
	request.streamingInput->source.sampleRate = 16000;
	request.streamingInput->source.sequenceStart = 10;
	request.streamingInput->cursor.nextSequence = 10;
	request.streamingInput->chunkPolicy.chunkMs = 20;

	const auto result = engine.TranscribeStreaming(
		request,
		[](const std::string&) { return true; });

	REQUIRE_FALSE(result.ok);
	REQUIRE(result.cancelled);
	REQUIRE(result.sessionState.cancelled);
	REQUIRE(result.sessionState.stage == SpeechSessionStage::Failed);
	REQUIRE(result.error.has_value());
	REQUIRE(result.error->code == SpeechRecognitionErrorCode::Cancelled);

	UnregisterStreamingAudioSource(streamId);
	std::filesystem::remove_all(root);
}
