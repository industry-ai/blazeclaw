#pragma once

#include <string>
#include "../config/ConfigModels.h"
#include "SpeechTranscriptionCoordinator.h"
#include "runtime/SpeechRecognition/SpeechRecognitionRuntime.h"

namespace blazeclaw::core::servicemanager_speech {

	// Returns true when speech runtime is enabled via environment/config.
	bool SpeechRuntimeEnabled();

	// Trigger speech runtime startup asynchronously. In the helper this is a
	// no-op or small forwarder; callers expect it to not throw.
	void StartSpeechRuntimeAsync();

	// Move the ApplySpeechRecognitionConfigReload orchestration out of
	// ServiceManager.cpp. This function preserves behavior while operating on
	// explicitly injected dependencies from the caller.
	bool ApplySpeechRecognitionConfigReload(
		bool running,
		blazeclaw::config::AppConfig& activeConfig,
		SpeechTranscriptionCoordinator& transcriptionCoordinator,
		speechrecognition::SpeechRecognitionRuntime& recognitionRuntime,
		speechrecognition::SpeechRecognitionRuntimeSnapshot& outSnapshot,
		std::string* outStatusMessage);

} // namespace blazeclaw::core::servicemanager_speech
