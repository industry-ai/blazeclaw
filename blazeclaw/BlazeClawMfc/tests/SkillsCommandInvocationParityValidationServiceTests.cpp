#include "pch.h"

#include "config/ConfigModels.h"
#include "core/SkillsCommandInvocationParityValidationService.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>

namespace {

	std::filesystem::path MakeTempWorkspace(const std::string& suffix) {
		const auto root = std::filesystem::temp_directory_path() /
			("blazeclaw_skills_phase5_parity_" + suffix + "_" + std::to_string(std::rand()));
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteUtf8File(const std::filesystem::path& filePath, const std::string& content) {
		std::filesystem::create_directories(filePath.parent_path());
		std::ofstream output(filePath, std::ios::binary);
		REQUIRE(output.is_open());
		output.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	std::string BuildSkill(
		const std::string& name,
		const std::string& description,
		const std::string& extraFrontmatter,
		const std::string& body) {
		std::string content;
		content += "---\n";
		content += "name: " + name + "\n";
		content += "description: " + description + "\n";
		if (!extraFrontmatter.empty()) {
			content += extraFrontmatter;
			if (!extraFrontmatter.ends_with("\n")) {
				content += "\n";
			}
		}
		content += "---\n";
		content += body;
		content += "\n";
		return content;
	}

	template<typename TEntry, typename TPredicate>
	const TEntry* FindBy(
		const std::vector<TEntry>& entries,
		TPredicate predicate) {
		const auto it = std::find_if(entries.begin(), entries.end(), predicate);
		return it == entries.end() ? nullptr : &(*it);
	}

} // namespace

TEST_CASE("Phase 5 command invocation parity report covers all required sections", "[skills][invocation][parity][phase5]") {
	const auto workspaceRoot = MakeTempWorkspace("full_report");

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "My Skill!" / "SKILL.md",
		BuildSkill("My Skill!", "Needs sanitization", "", "# My Skill"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "skill" / "SKILL.md",
		BuildSkill("skill", "Reserved collision baseline", "", "# Skill Reserved"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "dispatch-tool" / "SKILL.md",
		BuildSkill(
			"dispatch-tool",
			"Dispatch to tool",
			"command-dispatch: tool\ncommand-tool: tools.dispatch.run",
			"# Dispatch"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "note-skill" / "SKILL.md",
		BuildSkill(
			"note-skill",
			"Template rewrite",
			"command-prompt-template: Rewrite: {{args}}",
			"# Note"));

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.limits.maxCandidatesPerRoot = 64;
	appConfig.skills.limits.maxSkillsLoadedPerSource = 64;
	appConfig.skills.limits.maxSkillFileBytes = 256000;

	blazeclaw::core::SkillsCommandInvocationParityValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE_FALSE(report.sanitizationParity.empty());
	REQUIRE_FALSE(report.skillResolutionParity.empty());
	REQUIRE_FALSE(report.directResolutionParity.empty());
	REQUIRE_FALSE(report.dispatchParity.empty());
	REQUIRE_FALSE(report.rewriteParity.empty());

	const auto* sanitized = FindBy(
		report.sanitizationParity,
		[](const blazeclaw::core::SkillsCommandInvocationSanitizationParityEntry& entry) {
			return entry.skillName == L"My Skill!";
		});
	REQUIRE(sanitized != nullptr);
	REQUIRE(sanitized->expectedSanitizedCommand == L"my_skill");
	REQUIRE(sanitized->matches);

	const auto* dedupeReserved = FindBy(
		report.dedupeParity,
		[](const blazeclaw::core::SkillsCommandInvocationDedupeParityEntry& entry) {
			return entry.baseCommand == L"skill";
		});
	REQUIRE(dedupeReserved != nullptr);
	REQUIRE(dedupeReserved->reservedCollision);
	REQUIRE(dedupeReserved->deterministic);

	const auto* skillAliasDispatch = FindBy(
		report.skillResolutionParity,
		[](const blazeclaw::core::SkillsCommandInvocationSkillResolutionParityEntry& entry) {
			return entry.expectedSkillName == L"dispatch-tool";
		});
	REQUIRE(skillAliasDispatch != nullptr);
	REQUIRE(skillAliasDispatch->matches);

	const auto* skillAliasNote = FindBy(
		report.skillResolutionParity,
		[](const blazeclaw::core::SkillsCommandInvocationSkillResolutionParityEntry& entry) {
			return entry.expectedSkillName == L"note-skill";
		});
	REQUIRE(skillAliasNote != nullptr);
	REQUIRE(skillAliasNote->matches);

	const auto* directDispatch = FindBy(
		report.directResolutionParity,
		[](const blazeclaw::core::SkillsCommandInvocationDirectResolutionParityEntry& entry) {
			return entry.expectedCommandName == L"dispatch_tool";
		});
	REQUIRE(directDispatch != nullptr);
	REQUIRE(directDispatch->matches);

	const auto* directNote = FindBy(
		report.directResolutionParity,
		[](const blazeclaw::core::SkillsCommandInvocationDirectResolutionParityEntry& entry) {
			return entry.expectedCommandName == L"note_skill";
		});
	REQUIRE(directNote != nullptr);
	REQUIRE(directNote->matches);

	const auto* dispatchRow = FindBy(
		report.dispatchParity,
		[](const blazeclaw::core::SkillsCommandInvocationDispatchParityEntry& entry) {
			return entry.commandName == L"dispatch_tool";
		});
	REQUIRE(dispatchRow != nullptr);
	REQUIRE(dispatchRow->dispatchExpected);
	REQUIRE(dispatchRow->dispatchEnabled);
	REQUIRE(dispatchRow->matches);

	const auto* rewriteRow = FindBy(
		report.rewriteParity,
		[](const blazeclaw::core::SkillsCommandInvocationRewriteParityEntry& entry) {
			return entry.commandName == L"note_skill";
		});
	REQUIRE(rewriteRow != nullptr);
	REQUIRE(rewriteRow->rewriteExpected);
	REQUIRE(rewriteRow->rewriteProduced);
	REQUIRE(rewriteRow->matches);
	REQUIRE(rewriteRow->rewrittenPromptUtf8 == "Rewrite: hello");

	const auto markdown = service.BuildMarkdownReport(report);
	REQUIRE(markdown.find("Sanitized command name parity") != std::string::npos);
	REQUIRE(markdown.find("Reserved-name dedupe parity") != std::string::npos);
	REQUIRE(markdown.find("`/skill <name>` resolution parity") != std::string::npos);
	REQUIRE(markdown.find("Direct `/<command>` resolution parity") != std::string::npos);
	REQUIRE(markdown.find("`command-dispatch` parity") != std::string::npos);
	REQUIRE(markdown.find("Prompt-template rewrite parity") != std::string::npos);

	const auto json = service.BuildJsonReport(report);
	REQUIRE(json.find("\"sanitizationParity\"") != std::string::npos);
	REQUIRE(json.find("\"dedupeParity\"") != std::string::npos);
	REQUIRE(json.find("\"skillResolutionParity\"") != std::string::npos);
	REQUIRE(json.find("\"directResolutionParity\"") != std::string::npos);
	REQUIRE(json.find("\"dispatchParity\"") != std::string::npos);
	REQUIRE(json.find("\"rewriteParity\"") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}
