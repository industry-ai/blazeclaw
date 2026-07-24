#include "pch.h"
#include "ServiceManagerLocalModelHelpers.h"

#include <Windows.h>

namespace blazeclaw::core::servicemanager_localmodel {

	bool ResolveLocalModelActivationFromEnv() {
		wchar_t* raw = nullptr;
		size_t len = 0;
		if (_wdupenv_s(&raw, &len, L"BLAZECLAW_LOCAL_MODEL_ACTIVATION") != 0 || raw == nullptr) {
			if (raw) free(raw);
			return false;
		}

		std::wstring normalized;
		for (size_t i = 0; i < len && raw[i] != L'\0'; ++i) {
			normalized.push_back(std::tolower(raw[i]));
		}
		free(raw);

		if (normalized == L"1" || normalized == L"true" || normalized == L"yes" ||
			normalized == L"on") {
			return true;
		}

		if (normalized == L"0" || normalized == L"false" || normalized == L"no" ||
			normalized == L"off") {
			return false;
		}

		return false;
	}

	std::string BuildLocalModelActivationReason(
		const std::optional<std::string>& configReason,
		const bool envForced) {
		if (configReason.has_value() && !configReason->empty()) {
			return *configReason;
		}

		if (envForced) {
			return std::string("env_forced_activation");
		}

		return std::string();
	}

} // namespace blazeclaw::core::servicemanager_localmodel
