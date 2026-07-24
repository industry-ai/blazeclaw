#include "pch.h"
#include "ServiceManagerLocalModelHelpers.h"

#ifndef BLAZECLAW_TESTS_NO_RUNTIME_DEPENDENCIES
#include "runtime/LocalModel/LlamaTextGenerationRuntime.h"
#include "runtime/LocalModel/OnnxTextGenerationRuntime.h"
#endif

#include <algorithm>
#include <cwctype>
#include <Windows.h>

namespace blazeclaw::core::servicemanager_localmodel {

	namespace {

		std::wstring ToLower(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

	} // namespace

	std::unique_ptr<localmodel::ITextGenerationRuntime> BuildRuntimeForProvider(
		const std::wstring& provider) {
	#ifdef BLAZECLAW_TESTS_NO_RUNTIME_DEPENDENCIES
		(void)provider;
		return nullptr;
	#else
		const std::wstring normalizedProvider = ToLower(provider);
		if (normalizedProvider == L"llama" ||
			normalizedProvider == L"llama.cpp") {
			return std::make_unique<localmodel::LlamaTextGenerationRuntime>();
		}

		return std::make_unique<localmodel::OnnxTextGenerationRuntime>();
	#endif
	}

	bool ResolveLocalModelActivationFromEnv() {
		wchar_t* raw = nullptr;
		size_t len = 0;
		if (_wdupenv_s(&raw, &len, L"BLAZECLAW_LOCAL_MODEL_ACTIVATION") != 0 || raw == nullptr) {
			if (raw) free(raw);
			return false;
		}

		std::wstring normalized;
		for (size_t i = 0; i < len && raw[i] != L'\0'; ++i) {
			normalized.push_back(std::towlower(raw[i]));
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
