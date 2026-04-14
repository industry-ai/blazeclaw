#include "core/SkillsSkillContractCompat.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::CreateSyntheticSkillSourceInfoCompat;
using blazeclaw::core::SkillSourceOrigin;
using blazeclaw::core::SkillSourceScope;

TEST_CASE(
	"Skills skill-contract compat: synthetic source info uses OpenClaw-aligned defaults",
	"[skills][skill-contract][source-info][defaults]") {
	const auto info = CreateSyntheticSkillSourceInfoCompat(
		L"/workspace/skills/demo/SKILL.md",
		L"workspace");

	REQUIRE(info.path.wstring() == L"/workspace/skills/demo/SKILL.md");
	REQUIRE(info.source == L"workspace");
	REQUIRE(info.scope == SkillSourceScope::Temporary);
	REQUIRE(info.origin == SkillSourceOrigin::TopLevel);
	REQUIRE(info.baseDir.empty());
}

TEST_CASE(
	"Skills skill-contract compat: synthetic source info allows explicit overrides",
	"[skills][skill-contract][source-info][overrides]") {
	const auto info = CreateSyntheticSkillSourceInfoCompat(
		L"/workspace/skills/demo/SKILL.md",
		L"plugin",
		SkillSourceScope::Project,
		SkillSourceOrigin::Package,
		L"/workspace/skills/demo");

	REQUIRE(info.source == L"plugin");
	REQUIRE(info.scope == SkillSourceScope::Project);
	REQUIRE(info.origin == SkillSourceOrigin::Package);
	REQUIRE(info.baseDir.wstring() == L"/workspace/skills/demo");
}
