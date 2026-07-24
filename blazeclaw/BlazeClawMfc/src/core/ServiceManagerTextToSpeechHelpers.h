#pragma once

#include <string>
#include "../config/ConfigModels.h"
#include "runtime/TextToSpeech/ITextToSpeechRuntime.h"

namespace blazeclaw::core::servicemanager_tts {

	// Returns true when TTS is enabled via environment/config.
	bool TextToSpeechEnabled();

	// Start a TTS utterance and return an utterance id. Non-throwing.
	std::string StartTextToSpeech(const std::string& text, const std::string& voice, const std::string& model);

	// Stop a running utterance by id. Non-throwing.
	void StopTextToSpeech(const std::string& utteranceId);

	// Collect current TTS runtime snapshot for UI/tests.
	texttospeech::TextToSpeechRuntimeSnapshot CollectTextToSpeechSnapshot();

	// Stateful helpers for moving TTS snapshot ownership out of ServiceManager.
	texttospeech::TextToSpeechRuntimeSnapshot StartTextToSpeechState(
		bool running,
		const std::string& provider,
		const std::string& model,
		const std::string& voice,
		const std::string& runId);

	texttospeech::TextToSpeechRuntimeSnapshot StopTextToSpeechState(
		const std::string& utteranceId);

} // namespace blazeclaw::core::servicemanager_tts
