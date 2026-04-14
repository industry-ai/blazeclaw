#include "core/InlineActionsOrchestrationService.h"

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

using blazeclaw::core::InlineActionsOrchestrationService;

namespace {

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

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

TEST_CASE(
	"ServiceManager contract: inline invocation short-circuit and authorization policy are present",
	"[inline-actions][contract][servicemanager][step3-4]") {
	const auto serviceManagerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const std::string source = ReadTextFile(serviceManagerPath);

	REQUIRE(source.find("TryExecuteInlineToolInvocation(") != std::string::npos);
	REQUIRE(source.find("request.allowInlineToolImmediateExecution") !=
		std::string::npos);
	REQUIRE(source.find("request.inlineInvocationAuthorizedSender") !=
		std::string::npos);
	REQUIRE(source.find("request.inlineInvocationSenderIsOwner") !=
		std::string::npos);
	REQUIRE(source.find("inline_invocation_unauthorized_sender") !=
		std::string::npos);
	REQUIRE(source.find("inline_invocation_owner_required") !=
		std::string::npos);
	REQUIRE(source.find("ExtractInlineToolResultText(") != std::string::npos);
}
