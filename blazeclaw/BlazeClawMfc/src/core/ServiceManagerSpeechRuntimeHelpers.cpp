#include "pch.h"
#include "ServiceManagerSpeechRuntimeHelpers.h"

#include <Windows.h>
#include <cwctype>
#include <algorithm>
#include <string>

#include "../config/ConfigModels.h"

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

namespace blazeclaw::core::servicemanager_speech {

	static std::wstring TrimLocal(const std::wstring& value) {
		const auto first = std::find_if_not(
			value.begin(),
			value.end(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; });
		const auto last = std::find_if_not(
			value.rbegin(),
			value.rend(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; }).base();

		if (first >= last) {
			return {};
		}

		return std::wstring(first, last);
	}

	static std::wstring ToLowerLocal(const std::wstring& value) {
		std::wstring lowered = value;
		std::transform(lowered.begin(), lowered.end(), lowered.begin(),
			[](const wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
		return lowered;
	}

	bool ApplySpeechRecognitionConfigReload(
		bool running,
		blazeclaw::config::AppConfig& activeConfig,
		SpeechTranscriptionCoordinator& transcriptionCoordinator,
		speechrecognition::SpeechRecognitionRuntime& recognitionRuntime,
		speechrecognition::SpeechRecognitionRuntimeSnapshot& outSnapshot,
		std::string* outStatusMessage) {

		if (!running) {
			if (outStatusMessage != nullptr) {
				*outStatusMessage = "service_manager_not_running";
			}
			return false;
		}

		// Apply incoming config values into the active config reference so
		// the caller's state remains consistent with original ServiceManager
		// semantics.
		// Note: callers are expected to have already populated the activeConfig
		// speechRecognition fields; we operate directly on the provided object.

		transcriptionCoordinator.Shutdown(recognitionRuntime);
		recognitionRuntime.Configure(activeConfig);

		const std::wstring runtimeHotMode =
			ToLowerLocal(TrimLocal(activeConfig.speechRecognition.runtimeHotMode));
		const bool startupLoadEnabled =
			runtimeHotMode != L"on_demand" &&
			runtimeHotMode != L"idle_timeout";
		const bool loaded = startupLoadEnabled
			? recognitionRuntime.LoadModel()
			: true;

		outSnapshot = recognitionRuntime.Snapshot();
		if (!startupLoadEnabled) {
			outSnapshot.status = "startup_load_deferred";
		}
		else if (!loaded && outSnapshot.status.empty()) {
			outSnapshot.status = "load_failed";
		}

		if (outStatusMessage != nullptr) {
			*outStatusMessage = outSnapshot.status.empty()
				? (startupLoadEnabled ? std::string("loaded") : std::string("startup_load_deferred"))
				: outSnapshot.status;
		}

		return loaded;
	}

} // namespace blazeclaw::core::servicemanager_speech
