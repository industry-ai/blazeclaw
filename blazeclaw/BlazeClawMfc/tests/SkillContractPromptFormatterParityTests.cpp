#include "core/SkillsPromptFormatterCompat.h"

#include <catch2/catch_all.hpp>

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
