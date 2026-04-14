#include "pch.h"
#include "MarkdownFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <regex>
#include <vector>

namespace blazeclaw::core {

	namespace {

		std::wstring TrimFrontmatterCompat(const std::wstring& value);
		std::wstring ToLowerFrontmatterCompat(const std::wstring& value);
		std::wstring StripQuotesFrontmatterCompat(const std::wstring& value);

		bool IsYamlBooleanCompat(const std::wstring& value) {
			const std::wstring lowered = ToLowerFrontmatterCompat(TrimFrontmatterCompat(value));
			return lowered == L"true" || lowered == L"false";
		}

		bool IsYamlIntegerCompat(const std::wstring& value) {
			const std::wstring trimmed = TrimFrontmatterCompat(value);
			if (trimmed.empty()) {
				return false;
			}
			std::size_t index = 0;
			if (trimmed[0] == L'+' || trimmed[0] == L'-') {
				index = 1;
			}
			if (index >= trimmed.size()) {
				return false;
			}
			for (; index < trimmed.size(); ++index) {
				if (!std::iswdigit(trimmed[index])) {
					return false;
				}
			}
			return true;
		}

		std::optional<ParsedMarkdownFrontmatterYamlValueCompat>
			CoerceYamlFrontmatterValueCompat(const std::wstring& value) {
			const std::wstring trimmed = TrimFrontmatterCompat(value);
			if (trimmed.empty()) {
				return std::nullopt;
			}

			if (trimmed.starts_with(L"[") || trimmed.starts_with(L"{")) {
				return ParsedMarkdownFrontmatterYamlValueCompat{
					.value = trimmed,
					.kind = ParsedMarkdownFrontmatterYamlValueCompat::Kind::Structured,
				};
			}

			if (IsYamlBooleanCompat(trimmed) || IsYamlIntegerCompat(trimmed)) {
				return ParsedMarkdownFrontmatterYamlValueCompat{
					.value = trimmed,
					.kind = ParsedMarkdownFrontmatterYamlValueCompat::Kind::Scalar,
				};
			}

			const std::wstring stripped = StripQuotesFrontmatterCompat(trimmed);
			return ParsedMarkdownFrontmatterYamlValueCompat{
				.value = stripped,
				.kind = ParsedMarkdownFrontmatterYamlValueCompat::Kind::Scalar,
			};
		}

		bool IsYamlBlockScalarIndicatorCompat(const std::wstring& value) {
			static const std::wregex pattern(LR"(^[|>][+-]?(\d+)?[+-]?$)");
			return std::regex_match(TrimFrontmatterCompat(value), pattern);
		}

		bool ShouldPreferInlineLineValueCompat(
			const ParsedMarkdownFrontmatterLineEntryCompat& lineEntry,
			const ParsedMarkdownFrontmatterYamlValueCompat& yamlValue) {
			if (yamlValue.kind != ParsedMarkdownFrontmatterYamlValueCompat::Kind::Structured) {
				return false;
			}
			if (lineEntry.kind != ParsedMarkdownFrontmatterLineEntryCompat::Kind::Inline) {
				return false;
			}
			if (IsYamlBlockScalarIndicatorCompat(lineEntry.rawInline)) {
				return false;
			}
			return lineEntry.value.find(L":") != std::wstring::npos;
		}

		std::wstring TrimFrontmatterCompat(const std::wstring& value) {
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

		std::wstring ToLowerFrontmatterCompat(const std::wstring& value) {
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

		std::wstring StripQuotesFrontmatterCompat(const std::wstring& value) {
			const std::wstring trimmed = TrimFrontmatterCompat(value);
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

		std::wstring NormalizeNewlinesFrontmatterCompat(const std::wstring& content) {
			std::wstring normalized;
			normalized.reserve(content.size());
			for (std::size_t i = 0; i < content.size(); ++i) {
				const wchar_t ch = content[i];
				if (ch == L'\r') {
					if (i + 1 < content.size() && content[i + 1] == L'\n') {
						++i;
					}
					normalized.push_back(L'\n');
					continue;
				}
				normalized.push_back(ch);
			}
			return normalized;
		}

		std::vector<std::wstring> SplitLinesFrontmatterCompat(const std::wstring& value) {
			std::vector<std::wstring> lines;
			std::size_t cursor = 0;
			while (cursor <= value.size()) {
				const auto next = value.find(L'\n', cursor);
				if (next == std::wstring::npos) {
					lines.push_back(value.substr(cursor));
					break;
				}
				lines.push_back(value.substr(cursor, next - cursor));
				cursor = next + 1;
			}
			return lines;
		}

		std::optional<std::wstring> ExtractFrontmatterBlockCompat(
			const std::wstring& content,
			bool& outHasStart,
			bool& outHasEnd) {
			outHasStart = false;
			outHasEnd = false;

			const std::wstring normalized = NormalizeNewlinesFrontmatterCompat(content);
			if (!normalized.starts_with(L"---")) {
				return std::nullopt;
			}
			outHasStart = true;

			const auto endIndex = normalized.find(L"\n---", 3);
			if (endIndex == std::wstring::npos) {
				return std::nullopt;
			}
			outHasEnd = true;

			if (endIndex <= 4) {
				return std::wstring{};
			}
			return normalized.substr(4, endIndex - 4);
		}

		std::map<std::wstring, ParsedMarkdownFrontmatterLineEntryCompat>
			ParseLineFrontmatterCompat(const std::wstring& block) {
			std::map<std::wstring, ParsedMarkdownFrontmatterLineEntryCompat> result;
			const auto lines = SplitLinesFrontmatterCompat(block);
			for (std::size_t i = 0; i < lines.size(); ++i) {
				const std::wstring line = lines[i];
				const auto colonPos = line.find(L':');
				if (colonPos == std::wstring::npos) {
					continue;
				}

				const std::wstring key = ToLowerFrontmatterCompat(
					TrimFrontmatterCompat(line.substr(0, colonPos)));
				if (key.empty()) {
					continue;
				}

				const std::wstring inlineValue =
					TrimFrontmatterCompat(line.substr(colonPos + 1));
				if (inlineValue.empty() && i + 1 < lines.size()) {
					std::vector<std::wstring> valueLines;
					std::size_t j = i + 1;
					for (; j < lines.size(); ++j) {
						const std::wstring& next = lines[j];
						if (!next.empty() &&
							!next.starts_with(L" ") &&
							!next.starts_with(L"\t")) {
							break;
						}
						valueLines.push_back(next);
					}

					std::wstring combined;
					for (std::size_t k = 0; k < valueLines.size(); ++k) {
						if (k > 0) {
							combined += L"\n";
						}
						combined += valueLines[k];
					}

					const std::wstring multiValue = TrimFrontmatterCompat(combined);
					if (!multiValue.empty()) {
						result[key] = ParsedMarkdownFrontmatterLineEntryCompat{
							.value = multiValue,
							.kind = ParsedMarkdownFrontmatterLineEntryCompat::Kind::Multiline,
							.rawInline = inlineValue,
						};
					}
					i = j > 0 ? j - 1 : i;
					continue;
				}

				const std::wstring value = StripQuotesFrontmatterCompat(inlineValue);
				if (!value.empty()) {
					result[key] = ParsedMarkdownFrontmatterLineEntryCompat{
						.value = value,
						.kind = ParsedMarkdownFrontmatterLineEntryCompat::Kind::Inline,
						.rawInline = inlineValue,
					};
				}
			}

			return result;
		}

		std::optional<std::map<std::wstring, ParsedMarkdownFrontmatterYamlValueCompat>>
			ParseYamlFrontmatterCompat(const std::wstring& block) {
			std::map<std::wstring, ParsedMarkdownFrontmatterYamlValueCompat> result;
			const auto lines = SplitLinesFrontmatterCompat(block);
			for (const auto& line : lines) {
				const auto colonPos = line.find(L':');
				if (colonPos == std::wstring::npos) {
					continue;
				}

				const std::wstring key = TrimFrontmatterCompat(line.substr(0, colonPos));
				if (key.empty()) {
					continue;
				}

				const auto coerced = CoerceYamlFrontmatterValueCompat(
					line.substr(colonPos + 1));
				if (!coerced.has_value()) {
					continue;
				}
				result[key] = coerced.value();
			}

			return result;
		}

	} // namespace

	ParsedMarkdownFrontmatterCompat MergeMarkdownFrontmatterCompat(
		const std::map<std::wstring, ParsedMarkdownFrontmatterLineEntryCompat>& lineParsed,
		const std::optional<std::map<std::wstring, ParsedMarkdownFrontmatterYamlValueCompat>>& yamlParsed) {
		ParsedMarkdownFrontmatterCompat merged;
		if (!yamlParsed.has_value()) {
			for (const auto& [key, value] : lineParsed) {
				merged[key] = value.value;
			}
			return merged;
		}

		for (const auto& [key, yamlValue] : yamlParsed.value()) {
			const std::wstring normalizedKey = ToLowerFrontmatterCompat(TrimFrontmatterCompat(key));
			if (normalizedKey.empty()) {
				continue;
			}
			merged[normalizedKey] = yamlValue.value;
			const auto lineIt = lineParsed.find(normalizedKey);
			if (lineIt == lineParsed.end()) {
				continue;
			}
			if (ShouldPreferInlineLineValueCompat(lineIt->second, yamlValue)) {
				merged[normalizedKey] = lineIt->second.value;
			}
		}

		for (const auto& [key, lineEntry] : lineParsed) {
			if (merged.find(key) == merged.end()) {
				merged[key] = lineEntry.value;
			}
		}

		return merged;
	}

	MarkdownFrontmatterParseResultCompat ParseMarkdownFrontmatterBlockCompat(
		const std::wstring& content) {
		MarkdownFrontmatterParseResultCompat result;

		const auto block = ExtractFrontmatterBlockCompat(
			content,
			result.hasFrontmatterStart,
			result.hasFrontmatterEnd);
		if (!block.has_value()) {
			return result;
		}

		const auto lineParsed = ParseLineFrontmatterCompat(block.value());
		const auto yamlParsed = ParseYamlFrontmatterCompat(block.value());
		result.fields = MergeMarkdownFrontmatterCompat(lineParsed, yamlParsed);

		return result;
	}

} // namespace blazeclaw::core
