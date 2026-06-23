#include "pch.h"

#include "config/ConfigModels.h"
#include "core/SkillsFutureStabilityValidationService.h"

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

namespace {

	std::filesystem::path MakeTempWorkspace(const std::string& suffix) {
		const auto root = std::filesystem::temp_directory_path() /
			("blazeclaw_skills_phase7_future_stability_" + suffix + "_" + std::to_string(std::rand()));
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

TEST_CASE("Phase 7 future stability report flags all violation categories", "[skills][future-stability][phase7]") {
	const auto workspaceRoot = MakeTempWorkspace("violations");

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "workflow-skill" / "SKILL.md",
		BuildSkill(
			"workflow-skill",
			"workflow: rigid flow",
			"workflow-mode: strict",
			"# workflow"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "runtime-boundary-skill" / "SKILL.md",
		BuildSkill(
			"runtime-boundary-skill",
			"Boundary check",
			"blazeclaw-specific: true",
			"# boundary"));

	const auto unstableProtectedRoot = workspaceRoot / "unstable-skills-root";
	WriteUtf8File(
		unstableProtectedRoot / "self-evolving" / "SKILL.md",
		BuildSkill(
			"self-evolving",
			"Protected family in unstable location",
			"",
			"# protected"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "renamed-family" / "SKILL.md",
		BuildSkill(
			"renamed-family",
			"Rename without alias",
			"renamed-from: old-renamed-family",
			"# rename"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "invalid-frontmatter" / "SKILL.md",
		"name: invalid-frontmatter\ndescription: missing delimiter\n");

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.limits.maxCandidatesPerRoot = 128;
	appConfig.skills.limits.maxSkillsLoadedPerSource = 128;
	appConfig.skills.limits.maxSkillFileBytes = 256000;
	appConfig.skills.load.extraDirs.push_back(unstableProtectedRoot.wstring());

	blazeclaw::core::SkillsFutureStabilityValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE_FALSE(report.workflowAssumptionViolations.empty());
	REQUIRE_FALSE(report.runtimeBoundaryViolations.empty());
	REQUIRE_FALSE(report.protectedDirectoryViolations.empty());
	REQUIRE_FALSE(report.renameSafetyViolations.empty());
	REQUIRE_FALSE(report.frontmatterCompatibilityViolations.empty());
	REQUIRE_FALSE(report.compatibilityMatrix.empty());

	const auto* workflowRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"workflow-skill";
		});
	REQUIRE(workflowRow != nullptr);
	REQUIRE(workflowRow->workflowAssumptionStatus == blazeclaw::core::SkillsFutureStabilityStatus::Violation);

	const auto* boundaryRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"runtime-boundary-skill";
		});
	REQUIRE(boundaryRow != nullptr);
	REQUIRE(boundaryRow->runtimeBoundaryStatus == blazeclaw::core::SkillsFutureStabilityStatus::Violation);

	const auto* protectedRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"self-evolving";
		});
	REQUIRE(protectedRow != nullptr);
	REQUIRE(protectedRow->protectedDirectoryStatus == blazeclaw::core::SkillsFutureStabilityStatus::Violation);

	const auto* renameRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"renamed-family";
		});
	REQUIRE(renameRow != nullptr);
	REQUIRE(renameRow->renameSafetyStatus == blazeclaw::core::SkillsFutureStabilityStatus::Violation);

	const auto* frontmatterRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"invalid-frontmatter";
		});
	REQUIRE(frontmatterRow != nullptr);
	REQUIRE(frontmatterRow->frontmatterCompatibilityStatus == blazeclaw::core::SkillsFutureStabilityStatus::Violation);

	const auto markdown = service.BuildMarkdownReport(report);
	REQUIRE(markdown.find("Violation summary") != std::string::npos);
	REQUIRE(markdown.find("Workflow-specific assumption violations") != std::string::npos);
	REQUIRE(markdown.find("Runtime-adapter/config boundary violations") != std::string::npos);
	REQUIRE(markdown.find("Protected-directory stability violations") != std::string::npos);
	REQUIRE(markdown.find("Rename safety violations") != std::string::npos);
	REQUIRE(markdown.find("Frontmatter compatibility violations") != std::string::npos);
	REQUIRE(markdown.find("Compatibility matrix") != std::string::npos);

	const auto json = service.BuildJsonReport(report);
	REQUIRE(json.find("\"violationSummary\"") != std::string::npos);
	REQUIRE(json.find("\"workflowAssumptionViolations\"") != std::string::npos);
	REQUIRE(json.find("\"runtimeBoundaryViolations\"") != std::string::npos);
	REQUIRE(json.find("\"protectedDirectoryViolations\"") != std::string::npos);
	REQUIRE(json.find("\"renameSafetyViolations\"") != std::string::npos);
	REQUIRE(json.find("\"frontmatterCompatibilityViolations\"") != std::string::npos);
	REQUIRE(json.find("\"compatibilityMatrix\"") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}

TEST_CASE("Phase 7 future stability report keeps compliant skills clean", "[skills][future-stability][phase7]") {
	const auto workspaceRoot = MakeTempWorkspace("compliant");

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "web-browsing" / "SKILL.md",
		BuildSkill(
			"web-browsing",
			"OpenClaw-compatible browsing skill",
			"user-invocable: true\nrequires-env: BROWSER_TOKEN",
			"# browsing"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "renamed-family-safe" / "SKILL.md",
		BuildSkill(
			"renamed-family-safe",
			"Rename metadata with alias",
			"renamed-from: old-family\ncompatibility-alias: old-family",
			"# safe rename"));

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.limits.maxCandidatesPerRoot = 128;
	appConfig.skills.limits.maxSkillsLoadedPerSource = 128;
	appConfig.skills.limits.maxSkillFileBytes = 256000;

	blazeclaw::core::SkillsFutureStabilityValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE(report.workflowAssumptionViolations.empty());
	REQUIRE(report.runtimeBoundaryViolations.empty());
	REQUIRE(report.protectedDirectoryViolations.empty());
	REQUIRE(report.renameSafetyViolations.empty());
	REQUIRE(report.frontmatterCompatibilityViolations.empty());
	REQUIRE(report.compatibilityMatrix.size() >= 2);

	const auto* browsingRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"web-browsing";
		});
	REQUIRE(browsingRow != nullptr);
	REQUIRE(browsingRow->workflowAssumptionStatus == blazeclaw::core::SkillsFutureStabilityStatus::Compliant);
	REQUIRE(browsingRow->runtimeBoundaryStatus == blazeclaw::core::SkillsFutureStabilityStatus::Compliant);
	REQUIRE(browsingRow->protectedDirectoryStatus == blazeclaw::core::SkillsFutureStabilityStatus::Compliant);
	REQUIRE(browsingRow->frontmatterCompatibilityStatus == blazeclaw::core::SkillsFutureStabilityStatus::Compliant);

	const auto* renameSafeRow = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsFutureStabilityCompatibilityRow& row) {
			return row.skillName == L"renamed-family-safe";
		});
	REQUIRE(renameSafeRow != nullptr);
	REQUIRE(renameSafeRow->renameSafetyStatus == blazeclaw::core::SkillsFutureStabilityStatus::Compliant);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}