#include "pch.h"

#include "../src/app/VoiceRecorder.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

TEST_CASE("VoiceRecorder deterministic ingest populates ring, telemetry, and artifact", "[speech][recorder][phase_e]")
{
	CVoiceRecorder recorder;

	std::vector<int16_t> pcm(320, 1024);
	recorder.PushPcm16ChunkForTest(pcm.data(), pcm.size(), 1, 250);

	std::vector<float> latest;
	REQUIRE(recorder.ReadLatestSamples(latest, 160));
	REQUIRE(latest.size() == 160);

	const auto telemetry = recorder.GetTelemetrySnapshot();
	REQUIRE(telemetry.chunkEnqueueLatencyUs == 250);
	REQUIRE(telemetry.ringOccupancyPercent > 0);

	const auto artifact = recorder.BuildStreamingAudioArtifact();
	REQUIRE(artifact.has_value());
	REQUIRE(artifact->handoffMode == blazeclaw::core::speechrecognition::SpeechAudioHandoffMode::PcmStream);
	REQUIRE(artifact->sequenceEnd > artifact->sequenceStart);
}
