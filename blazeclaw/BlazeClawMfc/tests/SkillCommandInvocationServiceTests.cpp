#include "core/SkillCommandInvocationService.h"

#include <catch2/catch_all.hpp>

using blazeclaw::core::SkillCommandInvocationService;
using blazeclaw::core::SkillsCommandSpec;

TEST_CASE("SkillCommandInvocationService resolves direct command and args", "[skills][invocation][direct]") {
	SkillCommandInvocationService service;
	const std::vector<SkillsCommandSpec> commands = {
		SkillsCommandSpec{
			.name = L"demo_skill",
			.skillName = L"demo-skill",
			.description = L"Demo",
		},
	};

	const auto result = service.ResolveInvocation(L"/demo_skill do the thing", commands);
	REQUIRE(result.has_value());
	REQUIRE(result->command.skillName == L"demo-skill");
	REQUIRE(result->args.has_value());
	REQUIRE(result->args.value() == L"do the thing");
}

TEST_CASE("SkillCommandInvocationService resolves /skill normalized lookup", "[skills][invocation][skill-alias]") {
	SkillCommandInvocationService service;
	const std::vector<SkillsCommandSpec> commands = {
		SkillsCommandSpec{
			.name = L"demo_skill",
			.skillName = L"demo-skill",
			.description = L"Demo",
		},
	};

	const auto result = service.ResolveInvocation(L"/skill demo-skill", commands);
	REQUIRE(result.has_value());
	REQUIRE(result->command.name == L"demo_skill");
	REQUIRE_FALSE(result->args.has_value());
}

TEST_CASE("SkillCommandInvocationService resolves /skill with args", "[skills][invocation][skill-args]") {
	SkillCommandInvocationService service;
	const std::vector<SkillsCommandSpec> commands = {
		SkillsCommandSpec{
			.name = L"demo_skill",
			.skillName = L"demo-skill",
			.description = L"Demo",
		},
	};

	const auto result = service.ResolveInvocation(L"/skill demo_skill run now", commands);
	REQUIRE(result.has_value());
	REQUIRE(result->command.name == L"demo_skill");
	REQUIRE(result->args.has_value());
	REQUIRE(result->args.value() == L"run now");
}

TEST_CASE("SkillCommandInvocationService returns null for unknown command", "[skills][invocation][unknown]") {
	SkillCommandInvocationService service;
	const std::vector<SkillsCommandSpec> commands = {
		SkillsCommandSpec{
			.name = L"demo_skill",
			.skillName = L"demo-skill",
			.description = L"Demo",
		},
	};

	const auto result = service.ResolveInvocation(L"/unknown arg", commands);
	REQUIRE_FALSE(result.has_value());
}
