#include "pch.h"
#include "SkillsConfigEval.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
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

		std::optional<bool> ResolveConfigPathTruthyWithoutDefaults(
			const blazeclaw::config::AppConfig& appConfig,
			const std::wstring& normalizedPath) {
			if (normalizedPath == L"gateway.bind" || normalizedPath == L"gateway.bindaddress") {
				return !appConfig.gateway.bindAddress.empty();
			}

			if (normalizedPath == L"gateway.port") {
				return appConfig.gateway.port > 0;
			}

			if (normalizedPath == L"agent.model") {
				return !appConfig.agent.model.empty();
			}

			if (normalizedPath == L"agent.streaming") {
				return appConfig.agent.enableStreaming;
			}

			if (normalizedPath == L"skills.load.watch") {
				return appConfig.skills.load.watch;
			}

			if (normalizedPath == L"sandbox.browserenabled") {
				return appConfig.sandbox.browserEnabled;
			}

			if (normalizedPath == L"browser.enabled" ||
				normalizedPath == L"browser.evaluateenabled") {
				if (appConfig.sandbox.browserEnabled) {
					return true;
				}
				return std::nullopt;
			}

			return std::nullopt;
		}

	} // namespace

	const SkillsConfigEvalPolicy& DefaultSkillsConfigEvalPolicy() {
		static const SkillsConfigEvalPolicy kPolicy{
			.configPathDefaults = {
				{ L"browser.enabled", true },
				{ L"browser.evaluateenabled", true },
			},
		};

		return kPolicy;
	}

	std::wstring ResolveSkillsRuntimePlatform() {
#if defined(_WIN32)
		return L"win32";
#elif defined(__APPLE__)
		return L"darwin";
#elif defined(__linux__)
		return L"linux";
#else
		return L"unknown";
#endif
	}

	bool SkillsHasBinary(const std::wstring& binName) {
		const std::wstring name = Trim(binName);
		if (name.empty()) {
			return false;
		}

		const DWORD required = SearchPathW(
			nullptr,
			name.c_str(),
			nullptr,
			0,
			nullptr,
			nullptr);
		return required > 0;
	}

	std::vector<std::wstring> NormalizeSkillsAllowlistEntries(
		const std::vector<std::wstring>& rawEntries) {
		std::vector<std::wstring> normalized;
		normalized.reserve(rawEntries.size());

		for (const auto& entry : rawEntries) {
			const std::wstring value = ToLower(Trim(entry));
			if (value.empty()) {
				continue;
			}

			normalized.push_back(value);
		}

		std::sort(normalized.begin(), normalized.end());
		normalized.erase(
			std::unique(normalized.begin(), normalized.end()),
			normalized.end());
		return normalized;
	}

	bool IsSkillsConfigPathTruthy(
		const blazeclaw::config::AppConfig& appConfig,
		const std::wstring& configPath,
		const SkillsConfigEvalPolicy& policy) {
		const std::wstring normalizedPath = ToLower(Trim(configPath));
		if (normalizedPath.empty()) {
			return false;
		}

		const auto resolved = ResolveConfigPathTruthyWithoutDefaults(
			appConfig,
			normalizedPath);
		if (resolved.has_value()) {
			return resolved.value();
		}

		const auto fallback = policy.configPathDefaults.find(normalizedPath);
		if (fallback != policy.configPathDefaults.end()) {
			return fallback->second;
		}

		return false;
	}

} // namespace blazeclaw::core
