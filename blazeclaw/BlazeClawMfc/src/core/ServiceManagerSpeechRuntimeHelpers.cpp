#include "pch.h"
#include "ServiceManagerSpeechRuntimeHelpers.h"

#include <Windows.h>
#include <cwctype>

namespace blazeclaw::core::servicemanager_speech {

	bool SpeechRuntimeEnabled() {
		wchar_t* raw = nullptr;
		size_t len = 0;
		if (_wdupenv_s(&raw, &len, L"BLAZECLAW_SPEECH_RUNTIME_ENABLED") != 0 || raw == nullptr) {
			if (raw) free(raw);
			return false;
		}

		std::wstring normalized;
		for (size_t i = 0; i < len && raw[i] != L'\0'; ++i) {
			normalized.push_back(std::towlower(raw[i]));
		}
		free(raw);

		if (normalized == L"1" || normalized == L"true" || normalized == L"yes" || normalized == L"on") {
			return true;
		}

		return false;
	}

	void StartSpeechRuntimeAsync() {
		// Intentionally a thin forwarder/no-op for the focused extraction seam.
		// Actual startup orchestration remains in ServiceManager until fully
		// extracted; helper preserves a stable callsite for forwarding.
		return;
	}

} // namespace blazeclaw::core::servicemanager_speech
