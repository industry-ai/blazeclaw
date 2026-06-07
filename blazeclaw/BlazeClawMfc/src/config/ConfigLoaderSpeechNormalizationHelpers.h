#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace blazeclaw::config::speech_normalization {

	std::vector<std::wstring> SplitCsvValues(const std::wstring& raw);
	std::wstring NormalizeSpeechStreamingLatencyProfile(const std::wstring& raw);
	std::pair<std::uint32_t, std::uint32_t> ResolveSpeechStreamingPreviewProfile(
		const std::wstring& profile);
	std::vector<std::wstring> ParseSpeechHotwordsValue(const std::wstring& raw);
	void NormalizeSpeechHotwordsInPlace(std::vector<std::wstring>& hotwords);

} // namespace blazeclaw::config::speech_normalization
