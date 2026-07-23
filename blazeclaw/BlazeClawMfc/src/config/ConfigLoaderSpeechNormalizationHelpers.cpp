#include "pch.h"
#include "ConfigLoaderSpeechNormalizationHelpers.h"

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace blazeclaw::config::speech_normalization {

	namespace {

		std::wstring Trim(const std::wstring& value) {
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

		std::wstring ToLowerTrim(const std::wstring& raw) {
			std::wstring lowered = Trim(raw);
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		std::wstring TrimMatchingQuotes(const std::wstring& raw) {
			const std::wstring trimmed = Trim(raw);
			if (trimmed.size() >= 2) {
				const wchar_t first = trimmed.front();
				const wchar_t last = trimmed.back();
				if ((first == L'"' && last == L'"') ||
					(first == L'\'' && last == L'\'')) {
					return trimmed.substr(1, trimmed.size() - 2);
				}
			}

			return trimmed;
		}

	} // namespace

	std::vector<std::wstring> SplitCsvValues(const std::wstring& raw) {
		std::vector<std::wstring> values;
		std::wstring token;
		for (const wchar_t ch : raw) {
			if (ch == L',' || ch == L';') {
				const std::wstring trimmed = Trim(token);
				if (!trimmed.empty()) {
					values.push_back(trimmed);
				}
				token.clear();
				continue;
			}

			token.push_back(ch);
		}

		const std::wstring trimmed = Trim(token);
		if (!trimmed.empty()) {
			values.push_back(trimmed);
		}

		return values;
	}

	std::wstring NormalizeSpeechStreamingLatencyProfile(const std::wstring& raw) {
		const std::wstring normalized = ToLowerTrim(raw);
		if (normalized == L"balanced" ||
			normalized == L"low_latency" ||
			normalized == L"low-latency" ||
			normalized == L"aggressive") {
			return normalized == L"low-latency" ? L"low_latency" : normalized;
		}

		return L"balanced";
	}

	std::pair<std::uint32_t, std::uint32_t> ResolveSpeechStreamingPreviewProfile(
		const std::wstring& profile) {
		const std::wstring normalized = NormalizeSpeechStreamingLatencyProfile(profile);
		if (normalized == L"aggressive") {
			return { 240u, 160u };
		}

		if (normalized == L"low_latency") {
			return { 320u, 320u };
		}

		return { 640u, 320u };
	}

	std::vector<std::wstring> ParseSpeechHotwordsValue(const std::wstring& raw) {
		const std::wstring trimmed = Trim(raw);
		if (trimmed.empty()) {
			return {};
		}

		std::wstring body = trimmed;
		if (body.size() >= 2 && body.front() == L'[' && body.back() == L']') {
			body = body.substr(1, body.size() - 2);
		}

		auto values = SplitCsvValues(body);
		for (auto& value : values) {
			value = Trim(TrimMatchingQuotes(value));
		}

		values.erase(
			std::remove_if(
				values.begin(),
				values.end(),
				[](const std::wstring& value) { return value.empty(); }),
			values.end());
		return values;
	}

	void NormalizeSpeechHotwordsInPlace(std::vector<std::wstring>& hotwords) {
		std::vector<std::wstring> normalized;
		normalized.reserve(hotwords.size());
		std::unordered_set<std::wstring> seen;
		for (auto& hotword : hotwords) {
			hotword = Trim(hotword);
			if (hotword.empty()) {
				continue;
			}

			if (seen.insert(hotword).second) {
				normalized.push_back(hotword);
			}
		}

		hotwords = std::move(normalized);
	}

} // namespace blazeclaw::config::speech_normalization
