#include "pch.h"
#include "ServiceManagerTextToSpeechHelpers.h"

#include <Windows.h>
#include <cwctype>
#include <string>

namespace blazeclaw::core::servicemanager_tts {

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
		texttospeech::TextToSpeechRuntimeSnapshot snap;
		snap.speaking = false;
		snap.activeUtteranceId = "";
		snap.provider = "";
		snap.model = "";
		snap.voice = "";
		snap.status = "stopped";
		return snap;
	}

} // namespace blazeclaw::core::servicemanager_tts
