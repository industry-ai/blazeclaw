#pragma once

#include <map>
#include <optional>
#include <string>

namespace blazeclaw::core {

	struct ParsedMarkdownFrontmatterLineEntryCompat {
		std::wstring value;
		enum class Kind {
			Inline = 0,
			Multiline = 1,
		};
		Kind kind = Kind::Inline;
		std::wstring rawInline;
	};

	struct ParsedMarkdownFrontmatterYamlValueCompat {
		std::wstring value;
		enum class Kind {
			Scalar = 0,
			Structured = 1,
		};
		Kind kind = Kind::Scalar;
	};

	using ParsedMarkdownFrontmatterCompat =
		std::map<std::wstring, std::wstring>;

	struct MarkdownFrontmatterParseResultCompat {
		bool hasFrontmatterStart = false;
		bool hasFrontmatterEnd = false;
		ParsedMarkdownFrontmatterCompat fields;
	};

	[[nodiscard]] MarkdownFrontmatterParseResultCompat
		ParseMarkdownFrontmatterBlockCompat(const std::wstring& content);

	[[nodiscard]] ParsedMarkdownFrontmatterCompat
		MergeMarkdownFrontmatterCompat(
			const std::map<std::wstring, ParsedMarkdownFrontmatterLineEntryCompat>& lineParsed,
			const std::optional<std::map<std::wstring, ParsedMarkdownFrontmatterYamlValueCompat>>& yamlParsed);

} // namespace blazeclaw::core
