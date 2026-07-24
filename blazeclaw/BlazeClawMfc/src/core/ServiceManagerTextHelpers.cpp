#include "pch.h"
#include "ServiceManagerTextHelpers.h"

#include <algorithm>
#include <cwctype>
#include <cstdlib>
#include <Windows.h>

namespace blazeclaw::core::servicemanager_text {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

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

} // namespace blazeclaw::core::servicemanager_text

namespace blazeclaw::core::servicemanager_text {

	bool SuppressStartupMigrationsFromEnv() {
		wchar_t* raw = nullptr;
		size_t rawSize = 0;
		if (_wdupenv_s(
			&raw,
			&rawSize,
			L"BLAZECLAW_GATEWAY_SUPPRESS_STARTUP_MIGRATIONS") != 0 ||
			raw == nullptr) {
			return false;
		}

		const std::wstring normalized = ToLower(Trim(std::wstring(raw)));
		free(raw);
		return normalized == L"1" ||
			normalized == L"true" ||
			normalized == L"yes" ||
			normalized == L"on";
	}

	std::wstring Utf8ToWideLocal(const std::string& value) {
		if (value.empty()) {
			return {};
		}

		const int required = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (required <= 0) {
			return std::wstring(value.begin(), value.end());
		}

		std::wstring output(static_cast<std::size_t>(required), L'\0');
		const int converted = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			required);
		if (converted <= 0) {
			return std::wstring(value.begin(), value.end());
		}

		return output;
	}

	bool IsLlamaLocalModelId(const std::string& modelId) {
		return modelId.rfind("llama/", 0) == 0;
	}

	std::string ToNarrowAscii(const std::wstring& value) {
		std::string output;
		output.reserve(value.size());

		for (const wchar_t ch : value) {
			output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
		}

		return output;
	}

} // namespace blazeclaw::core::servicemanager_text
