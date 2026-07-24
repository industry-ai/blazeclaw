#include "pch.h"
#include <catch2/catch_all.hpp>

#include "../src/core/ServiceManagerTextToSpeechHelpers.h"

using namespace blazeclaw::core::servicemanager_tts;

TEST_CASE("[servicemanager][tts] TextToSpeechEnabled env parsing", "[servicemanager][tts]") {
	// Rely on the helper not throwing for missing env
	REQUIRE(TextToSpeechEnabled() == false);
}

TEST_CASE("[servicemanager][tts] Start/Stop non-throwing behavior", "[servicemanager][tts]") {
	auto id = StartTextToSpeech("hello","default","default");
	REQUIRE(!id.empty());
	StopTextToSpeech(id);
}

TEST_CASE("[servicemanager][tts] Snapshot default values", "[servicemanager][tts]") {
	auto snap = CollectTextToSpeechSnapshot();
	REQUIRE(snap.status == "stopped");
}
