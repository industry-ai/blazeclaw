#include "pch.h"
#include "ServiceManagerTextToSpeechHelpers.h"

#include <Windows.h>
#include <cwctype>
#include <mutex>
#include <string>

namespace blazeclaw::core::servicemanager_tts {
	namespace {
		std::mutex g_ttsStateMutex;
		texttospeech::TextToSpeechRuntimeSnapshot g_ttsState{};
	}

	bool TextToSpeechEnabled() {
		wchar_t* raw = nullptr;
		size_t len = 0;
		if (_wdupenv_s(&raw, &len, L"BLAZECLAW_TTS_ENABLED") != 0 || raw == nullptr) {
			if (raw) free(raw);
			return false;
		}

		std::wstring normalized;
		for (size_t i = 0; i < len && raw[i] != L'\0'; ++i) {
			normalized.push_back(std::towlower(raw[i]));
		}
		free(raw);

		return normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on";
	}

	std::string StartTextToSpeech(const std::string& text, const std::string& voice, const std::string& model) {
		// Minimal non-throwing forwarder for focused extraction seam.
		// Actual orchestration remains in ServiceManager until fully extracted.
		static int counter = 0;
		++counter;
		return std::string("utterance-") + std::to_string(counter);
	}

	void StopTextToSpeech(const std::string& utteranceId) {
		// No-op forwarder for focused extraction seam.
		(void)utteranceId;
	}

	texttospeech::TextToSpeechRuntimeSnapshot CollectTextToSpeechSnapshot() {
		std::lock_guard<std::mutex> guard(g_ttsStateMutex);
		if (g_ttsState.status.empty()) {
			g_ttsState.status = "stopped";
		}
		return g_ttsState;
	}

	texttospeech::TextToSpeechRuntimeSnapshot StartTextToSpeechState(
		bool running,
		const std::string& provider,
		const std::string& model,
		const std::string& voice,
		const std::string& runId) {
		std::lock_guard<std::mutex> guard(g_ttsStateMutex);

		g_ttsState.speakRequestsStarted += 1;
		g_ttsState.enabled = true;
		g_ttsState.ready = running;
		g_ttsState.provider = provider.empty() ? "default" : provider;
		g_ttsState.model = model.empty() ? "default" : model;
		g_ttsState.voice = voice.empty() ? "default" : voice;
		g_ttsState.activeUtteranceId =
			runId.empty()
			? std::string("utterance-") + std::to_string(g_ttsState.speakRequestsStarted)
			: runId + "-" + std::to_string(g_ttsState.speakRequestsStarted);
		g_ttsState.speaking = true;
		g_ttsState.status = "speaking";
		g_ttsState.error.reset();
		g_ttsState.speakRequestsCompleted += 1;

		return g_ttsState;
	}

	texttospeech::TextToSpeechRuntimeSnapshot StopTextToSpeechState(
		const std::string& utteranceId) {
		std::lock_guard<std::mutex> guard(g_ttsStateMutex);

		g_ttsState.stopRequests += 1;
		g_ttsState.speaking = false;
		g_ttsState.status = "stopped";
		if (!utteranceId.empty()) {
			g_ttsState.activeUtteranceId = utteranceId;
		}

		return g_ttsState;
	}

} // namespace blazeclaw::core::servicemanager_tts
