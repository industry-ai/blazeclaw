#include "core/SkillsPromptFormatterCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::FormatSkillsForPromptCompat;
using blazeclaw::core::FormatSkillsForPromptCompactCompat;
using blazeclaw::core::SkillPromptProjectionCompat;

TEST_CASE(
	"Skills prompt formatter compat: compact formatter uses description-matching guidance wording",
	"[skills][skill-contract][formatter][wording]") {
	const std::vector<SkillPromptProjectionCompat> skills = {
		SkillPromptProjectionCompat{
			.name = L"demo",
			.description = L"Demo description",
			.filePath = L"/skills/demo/SKILL.md",
			.baseDir = L"/skills/demo",
			.legacySource = L"workspace",
		},
	};

	const auto prompt = FormatSkillsForPromptCompactCompat(skills);
	REQUIRE(prompt.find(L"matches its description") != std::wstring::npos);
	REQUIRE(prompt.find(L"matches its name") == std::wstring::npos);
}

TEST_CASE(
	"Skills prompt formatter compat: full formatter preserves XML shape and escaping",
	"[skills][skill-contract][formatter][xml-shape]") {
	const std::vector<SkillPromptProjectionCompat> skills = {
		SkillPromptProjectionCompat{
			.name = L"a<b&c",
			.description = L"x>y\"z'",
			.filePath = L"/skills/a&b/SKILL.md",
			.baseDir = L"/skills/a&b",
			.legacySource = L"workspace",
		},
	};

	const auto prompt = FormatSkillsForPromptCompat(skills);
	REQUIRE(prompt.find(L"<available_skills>") != std::wstring::npos);
	REQUIRE(prompt.find(L"</available_skills>") != std::wstring::npos);
	REQUIRE(prompt.find(L"<name>a&lt;b&amp;c</name>") != std::wstring::npos);
	REQUIRE(prompt.find(L"<description>x&gt;y&quot;z&apos;</description>") !=
		std::wstring::npos);
	REQUIRE(prompt.find(L"<location>/skills/a&amp;b/SKILL.md</location>") !=
		std::wstring::npos);
}

TEST_CASE(
	"Skills prompt formatter compat: formatter preserves deterministic input ordering",
	"[skills][skill-contract][formatter][ordering]") {
	const std::vector<SkillPromptProjectionCompat> skills = {
		SkillPromptProjectionCompat{
			.name = L"skill-z",
			.description = L"z",
			.filePath = L"/skills/skill-z/SKILL.md",
			.baseDir = L"/skills/skill-z",
			.legacySource = L"workspace",
		},
		SkillPromptProjectionCompat{
			.name = L"skill-a",
			.description = L"a",
			.filePath = L"/skills/skill-a/SKILL.md",
			.baseDir = L"/skills/skill-a",
			.legacySource = L"workspace",
		},
	};

	const auto prompt = FormatSkillsForPromptCompactCompat(skills);
	const auto first = prompt.find(L"<name>skill-z</name>");
	const auto second = prompt.find(L"<name>skill-a</name>");
	REQUIRE(first != std::wstring::npos);
	REQUIRE(second != std::wstring::npos);
	REQUIRE(first < second);
}
