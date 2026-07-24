#pragma once

#include <string>

namespace blazeclaw::core::servicemanager_text {

	bool SuppressStartupMigrationsFromEnv();
	std::wstring Trim(const std::wstring& value);
	std::wstring ToLower(const std::wstring& value);
	std::wstring Utf8ToWideLocal(const std::string& value);
	bool IsLlamaLocalModelId(const std::string& modelId);
	std::string ToNarrowAscii(const std::wstring& value);

} // namespace blazeclaw::core::servicemanager_text
