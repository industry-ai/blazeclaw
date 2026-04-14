#include "core/MarkdownFrontmatterCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::MergeMarkdownFrontmatterCompat;
using blazeclaw::core::MarkdownFrontmatterParseResultCompat;
using blazeclaw::core::ParseMarkdownFrontmatterBlockCompat;
using blazeclaw::core::ParsedMarkdownFrontmatterLineEntryCompat;
using blazeclaw::core::ParsedMarkdownFrontmatterYamlValueCompat;

TEST_CASE(
	"Markdown frontmatter compat: extracts normalized block fields",
	"[markdown][frontmatter][compat][block]") {
	const std::wstring content =
		L"---\r\n"
		L"name: sample\r\n"
		L"description: sample description\r\n"
		L"---\r\n"
		L"body\r\n";

	const MarkdownFrontmatterParseResultCompat parsed =
		ParseMarkdownFrontmatterBlockCompat(content);
	REQUIRE(parsed.hasFrontmatterStart);
	REQUIRE(parsed.hasFrontmatterEnd);
	REQUIRE(parsed.fields.contains(L"name"));
	REQUIRE(parsed.fields.at(L"name") == L"sample");
}

TEST_CASE(
	"Markdown frontmatter compat: supports multiline line values",
	"[markdown][frontmatter][compat][multiline]") {
	const std::wstring content =
		L"---\n"
		L"name: sample\n"
		L"notes:\n"
		L"  line one\n"
		L"  line two\n"
		L"description: done\n"
		L"---\n";

	const auto parsed = ParseMarkdownFrontmatterBlockCompat(content);
	REQUIRE(parsed.fields.contains(L"notes"));
	REQUIRE(parsed.fields.at(L"notes").find(L"line one") != std::wstring::npos);
	REQUIRE(parsed.fields.at(L"notes").find(L"line two") != std::wstring::npos);
}

TEST_CASE(
	"Markdown frontmatter compat: structured yaml merge prefers inline colon value",
	"[markdown][frontmatter][compat][merge]") {
	std::map<std::wstring, ParsedMarkdownFrontmatterLineEntryCompat> lineParsed;
	lineParsed.emplace(
		L"config",
		ParsedMarkdownFrontmatterLineEntryCompat{
			.value = L"runtime:strict",
			.kind = ParsedMarkdownFrontmatterLineEntryCompat::Kind::Inline,
			.rawInline = L"runtime:strict",
		});

	std::map<std::wstring, ParsedMarkdownFrontmatterYamlValueCompat> yamlParsed;
	yamlParsed.emplace(
		L"config",
		ParsedMarkdownFrontmatterYamlValueCompat{
			.value = L"{\"runtime\":\"strict\"}",
			.kind = ParsedMarkdownFrontmatterYamlValueCompat::Kind::Structured,
		});

	const auto merged = MergeMarkdownFrontmatterCompat(lineParsed, yamlParsed);
	REQUIRE(merged.contains(L"config"));
	REQUIRE(merged.at(L"config") == L"runtime:strict");
}
