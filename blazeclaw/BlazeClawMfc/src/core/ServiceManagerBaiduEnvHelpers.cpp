#include "pch.h"
#include "ServiceManagerBaiduEnvHelpers.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>
#include <Windows.h>

namespace blazeclaw::core::servicemanager_baidu_env {

	std::optional<std::wstring> ResolveBaiduApiKeyFromPersistedConfig() {
		auto trimLocal = [](const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; });
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; })
				.base();

			if (first >= last) {
				return std::wstring{};
			}

			return std::wstring(first, last);
		};

		auto toLowerWideLocal = [](std::wstring value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return value;
		};

		std::vector<std::filesystem::path> candidates;

		const std::vector<std::wstring> configFolders = {
			L"baidu-search",
			L"baidu-search-search-web",
			L"baidu-search-search",
			L"baidu_search_search_web",
		};

		wchar_t profilePath[MAX_PATH]{};
		const DWORD chars = GetEnvironmentVariableW(
			L"USERPROFILE",
			profilePath,
			MAX_PATH);
		if (chars > 0 && chars < MAX_PATH) {
			for (const auto& folder : configFolders) {
				candidates.push_back(
					std::filesystem::path(profilePath) /
					L".config" /
					folder /
					L".env");
			}
		}

		std::error_code ec;
		const auto cwd = std::filesystem::current_path(ec);
		if (!ec) {
			candidates.push_back(
				cwd /
				L"blazeclaw" /
				L"skills" /
				L"baidu-search" /
				L".env");
			candidates.push_back(
				cwd /
				L"skills" /
				L"baidu-search" /
				L".env");
		}

		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
			std::filesystem::path cursor =
				std::filesystem::path(modulePath).parent_path();
			while (!cursor.empty()) {
				candidates.push_back(
					cursor /
					L"blazeclaw" /
					L"skills" /
					L"baidu-search" /
					L".env");
				candidates.push_back(
					cursor /
					L"skills" /
					L"baidu-search" /
					L".env");

				if (!cursor.has_parent_path()) {
					break;
				}

				auto parent = cursor.parent_path();
				if (parent == cursor) {
					break;
				}

				cursor = parent;
			}
		}

		for (const auto& path : candidates) {
			std::error_code existsError;
			if (!std::filesystem::exists(path, existsError) || existsError) {
				continue;
			}

			std::wifstream input(path);
			if (!input.is_open()) {
				continue;
			}

			std::wstring line;
			while (std::getline(input, line)) {
				const std::wstring trimmedLine = trimLocal(line);
				if (trimmedLine.empty() || trimmedLine.starts_with(L"#")) {
					continue;
				}

				const auto equals = trimmedLine.find(L'=');
				if (equals == std::wstring::npos || equals == 0) {
					continue;
				}

				const std::wstring key =
					toLowerWideLocal(trimLocal(trimmedLine.substr(0, equals)));
				std::wstring value = trimLocal(trimmedLine.substr(equals + 1));
				if (value.size() >= 2 &&
					((value.front() == L'"' && value.back() == L'"') ||
						(value.front() == L'\'' && value.back() == L'\''))) {
					value = value.substr(1, value.size() - 2);
				}

				if (value.empty()) {
					continue;
				}

				if (key == L"baidu_api_key" || key == L"api_key") {
					return value;
				}
			}
		}

		return std::nullopt;
	}

} // namespace blazeclaw::core::servicemanager_baidu_env
