#pragma once

#include <string>

namespace blazeclaw::core::servicemanager_speech {

	// Returns true when speech runtime is enabled via environment/config.
	bool SpeechRuntimeEnabled();

	// Trigger speech runtime startup asynchronously. In the helper this is a
	// no-op or small forwarder; callers expect it to not throw.
	void StartSpeechRuntimeAsync();

} // namespace blazeclaw::core::servicemanager_speech
