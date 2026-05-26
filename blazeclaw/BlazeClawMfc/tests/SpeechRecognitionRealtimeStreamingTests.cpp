#include "pch.h"

#include "../src/core/runtime/SpeechRecognition/SpeechRecognitionContracts.h"
#include "../src/core/runtime/SpeechRecognition/StreamingAudioSourceRegistry.h"
#include "../src/core/runtime/SpeechRecognition/SpeechModelLayoutProbe.h"
#include "../src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

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
		out << "stub";
	}

	blazeclaw::core::speechrecognition::SpeechModelLayoutProbeResult BuildSherpaLayout(
		const std::filesystem::path& root)
	{
		TouchFile(root / L"encoder-epoch-99-avg-1.onnx");
		TouchFile(root / L"decoder-epoch-99-avg-1.onnx");
		TouchFile(root / L"joiner-epoch-99-avg-1.onnx");
		TouchFile(root / L"tokens.txt");
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
	REQUIRE(engine.Load(root, layout, loadError));

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
	REQUIRE(result.sessionState.segment.has_value());
	REQUIRE(result.sessionState.segment->final);
	REQUIRE(result.sessionState.segment->sequence == 1);
	REQUIRE(result.sessionState.transcriptText == "[sherpa-streaming] speech detected");
	REQUIRE(result.sessionState.latencyMs > 0);

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
	REQUIRE(engine.Load(root, layout, loadError));

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
	REQUIRE(result.sessionState.segment.has_value());
	REQUIRE(result.sessionState.segment->final);

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
	REQUIRE(engine.Load(root, layout, loadError));

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
