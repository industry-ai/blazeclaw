#include "pch.h"

#include "config/ConfigModels.h"
#include "core/SkillsExecutionReadinessValidationService.h"

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
			("blazeclaw_skills_phase6_readiness_" + suffix + "_" + std::to_string(std::rand()));
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

TEST_CASE("Phase 6 execution readiness report covers dependency/mode/mapping/smoke/matrix", "[skills][execution-readiness][phase6]") {
	const auto workspaceRoot = MakeTempWorkspace("full_report");

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "local-ready" / "SKILL.md",
		BuildSkill(
			"local-ready",
			"Local ready skill",
			"requires: { bins: [\"cmd\"], env: [], config: [] }\ncommand-dispatch: tool\ncommand-tool: runtime.local.ready",
			"# Local Ready"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "missing-env" / "SKILL.md",
		BuildSkill(
			"missing-env",
			"Missing env skill",
			"requires-env: MISSING_PHASE6_ENV",
			"# Missing Env"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "unmapped-tool" / "SKILL.md",
		BuildSkill(
			"unmapped-tool",
			"No tool mapping skill",
			"",
			"# Unmapped"));

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.limits.maxCandidatesPerRoot = 64;
	appConfig.skills.limits.maxSkillsLoadedPerSource = 64;
	appConfig.skills.limits.maxSkillFileBytes = 256000;
	appConfig.skills.remoteEligibility.enabled = true;

	blazeclaw::core::SkillsExecutionReadinessValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE_FALSE(report.dependencyEntries.empty());
	REQUIRE_FALSE(report.modeEntries.empty());
	REQUIRE_FALSE(report.toolMappingEntries.empty());
	REQUIRE_FALSE(report.smokeEntries.empty());
	REQUIRE_FALSE(report.compatibilityMatrix.empty());

	const auto* localDependency = FindBy(
		report.dependencyEntries,
		[](const blazeclaw::core::SkillsExecutionDependencyEntry& entry) {
			return entry.familyKey == L"local-ready";
		});
	REQUIRE(localDependency != nullptr);
	REQUIRE(localDependency->ready);

	const auto* missingEnvDependency = FindBy(
		report.dependencyEntries,
		[](const blazeclaw::core::SkillsExecutionDependencyEntry& entry) {
			return entry.familyKey == L"missing-env";
		});
	REQUIRE(missingEnvDependency != nullptr);
	REQUIRE_FALSE(missingEnvDependency->ready);

	const auto* localMode = FindBy(
		report.modeEntries,
		[](const blazeclaw::core::SkillsExecutionModeEntry& entry) {
			return entry.familyKey == L"local-ready";
		});
	REQUIRE(localMode != nullptr);
	REQUIRE(localMode->mode == blazeclaw::core::SkillsExecutionReadinessMode::HostLocal);

	const auto* localMapping = FindBy(
		report.toolMappingEntries,
		[](const blazeclaw::core::SkillsExecutionToolMappingEntry& entry) {
			return entry.familyKey == L"local-ready";
		});
	REQUIRE(localMapping != nullptr);
	REQUIRE(localMapping->mapped);
	REQUIRE(localMapping->blazeClawToolOrAdapter == L"runtime.local.ready");

	const auto* unmappedMapping = FindBy(
		report.toolMappingEntries,
		[](const blazeclaw::core::SkillsExecutionToolMappingEntry& entry) {
			return entry.familyKey == L"unmapped-tool";
		});
	REQUIRE(unmappedMapping != nullptr);
	REQUIRE_FALSE(unmappedMapping->mapped);

	const auto* localSmoke = FindBy(
		report.smokeEntries,
		[](const blazeclaw::core::SkillsExecutionSmokeEntry& entry) {
			return entry.familyKey == L"local-ready";
		});
	REQUIRE(localSmoke != nullptr);
	REQUIRE(localSmoke->status == blazeclaw::core::SkillsExecutionReadinessSmokeStatus::Passed);

	const auto* missingEnvSmoke = FindBy(
		report.smokeEntries,
		[](const blazeclaw::core::SkillsExecutionSmokeEntry& entry) {
			return entry.familyKey == L"missing-env";
		});
	REQUIRE(missingEnvSmoke != nullptr);
	REQUIRE(missingEnvSmoke->status == blazeclaw::core::SkillsExecutionReadinessSmokeStatus::Skipped);

	const auto* localMatrix = FindBy(
		report.compatibilityMatrix,
		[](const blazeclaw::core::SkillsExecutionCompatibilityMatrixRow& entry) {
			return entry.familyKey == L"local-ready";
		});
	REQUIRE(localMatrix != nullptr);
	REQUIRE(localMatrix->dependenciesReady);
	REQUIRE(localMatrix->toolMappingReady);
	REQUIRE(localMatrix->smokeStatus == blazeclaw::core::SkillsExecutionReadinessSmokeStatus::Passed);

	const auto markdown = service.BuildMarkdownReport(report);
	REQUIRE(markdown.find("Dependency requirements") != std::string::npos);
	REQUIRE(markdown.find("Execution mode classification") != std::string::npos);
	REQUIRE(markdown.find("OpenClaw-to-BlazeClaw tool mapping") != std::string::npos);
	REQUIRE(markdown.find("Smoke-test gating and outcomes") != std::string::npos);
	REQUIRE(markdown.find("Compatibility matrix") != std::string::npos);

	const auto json = service.BuildJsonReport(report);
	REQUIRE(json.find("\"dependencyEntries\"") != std::string::npos);
	REQUIRE(json.find("\"modeEntries\"") != std::string::npos);
	REQUIRE(json.find("\"toolMappingEntries\"") != std::string::npos);
	REQUIRE(json.find("\"smokeEntries\"") != std::string::npos);
	REQUIRE(json.find("\"compatibilityMatrix\"") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}
