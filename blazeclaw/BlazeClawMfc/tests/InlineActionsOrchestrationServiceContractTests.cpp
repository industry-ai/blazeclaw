#include "core/InlineActionsOrchestrationService.h"

#include <catch2/catch_all.hpp>

#include <string>
#include <unordered_set>

using blazeclaw::core::InlineActionsOrchestrationService;

TEST_CASE(
	"InlineActionsOrchestrationService resolves slash command names",
	"[inline-actions][contract][slash-name]") {
	InlineActionsOrchestrationService service;

	REQUIRE(service.ResolveSlashCommandName("/skill weather") == "skill");
	REQUIRE(service.ResolveSlashCommandName(" /STATUS ") == "status");
	REQUIRE(service.ResolveSlashCommandName("not-a-command").empty());
}

TEST_CASE(
	"InlineActionsOrchestrationService builds builtin slash command namespace",
	"[inline-actions][contract][builtin]") {
	InlineActionsOrchestrationService service;
	const std::unordered_set<std::string> reserved = {
		"help",
		"skill",
		"tool",
	};

	const auto builtin = service.BuildBuiltinSlashCommands(reserved);
	REQUIRE(builtin.find("help") != builtin.end());
	REQUIRE(builtin.find("status") != builtin.end());
	REQUIRE(builtin.find("queue") != builtin.end());
	REQUIRE(builtin.find("skill") != builtin.end());
}

TEST_CASE(
	"InlineActionsOrchestrationService slash gate matches OpenClaw-style loading policy",
	"[inline-actions][contract][slash-gate]") {
	InlineActionsOrchestrationService service;
	const auto builtin = service.BuildBuiltinSlashCommands({ "help", "status", "skill" });

	REQUIRE_FALSE(service.ShouldLoadSkillCommandsForSlash(false, "skill", builtin));
	REQUIRE(service.ShouldLoadSkillCommandsForSlash(true, "skill", builtin));
	REQUIRE_FALSE(service.ShouldLoadSkillCommandsForSlash(true, "status", builtin));
	REQUIRE(service.ShouldLoadSkillCommandsForSlash(true, "weather", builtin));
	REQUIRE_FALSE(service.ShouldLoadSkillCommandsForSlash(true, std::string{}, builtin));
}
