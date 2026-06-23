#include "pch.h"

#include "config/ConfigModels.h"
#include "core/SkillsCatalogPromptParityValidationService.h"

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
			("blazeclaw_skills_phase4_parity_" + suffix + "_" + std::to_string(std::rand()));
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

	template<typename TEntry>
	const TEntry* FindByName(const std::vector<TEntry>& entries, const std::wstring& skillName) {
		const auto it = std::find_if(
			entries.begin(),
			entries.end(),
			[&skillName](const TEntry& entry) {
				return entry.skillName == skillName;
			});
		return it == entries.end() ? nullptr : &(*it);
	}

} // namespace

TEST_CASE("Phase 4 catalog and prompt parity report covers all required sections", "[skills][catalog][parity][phase4]") {
	const auto workspaceRoot = MakeTempWorkspace("full_report");

	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "parity-visible" / "SKILL.md",
		BuildSkill(
			"parity-visible",
			"Visible parity skill",
			"metadata: {\"openclaw\":{\"emoji\":\":sparkles:\"}}",
			"# Visible"));
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "parity-hidden" / "SKILL.md",
		BuildSkill(
			"parity-hidden",
			"Hidden parity skill",
			"disable-model-invocation: true",
			"# Hidden"));
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "parity-source" / "SKILL.md",
		BuildSkill(
			"parity-source",
			"OpenClaw source baseline",
			"",
			"# Source"));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "parity-visible" / "SKILL.md",
		BuildSkill(
			"parity-visible",
			"Visible parity skill",
			"metadata: {\"openclaw\":{\"emoji\":\":sparkles:\"}}",
			"# Visible"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "parity-hidden" / "SKILL.md",
		BuildSkill(
			"parity-hidden",
			"Hidden parity skill",
			"disable-model-invocation: true",
			"# Hidden"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "parity-source" / "SKILL.md",
		BuildSkill(
			"parity-source",
			"Bundled source candidate",
			"",
			"# Bundled Source"));

	WriteUtf8File(
		workspaceRoot / "skills" / "parity-source" / "SKILL.md",
		BuildSkill(
			"parity-source",
			"Workspace source winner",
			"",
			"# Workspace Source"));

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.limits.maxCandidatesPerRoot = 64;
	appConfig.skills.limits.maxSkillsLoadedPerSource = 64;
	appConfig.skills.limits.maxSkillsInPrompt = 64;
	appConfig.skills.limits.maxSkillsPromptChars = 64000;
	appConfig.skills.limits.maxSkillFileBytes = 256000;
	appConfig.skills.load.rejectPathSymlink = true;
	appConfig.skills.load.strictFrontmatter = false;

	blazeclaw::core::SkillsCatalogPromptParityValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE(report.openClawBundledRootFound);
	REQUIRE(report.blazeClawBundledRootFound);

	const auto* visibleCatalog = FindByName(report.catalogEntries, L"parity-visible");
	REQUIRE(visibleCatalog != nullptr);
	REQUIRE(visibleCatalog->catalogPresent);
	REQUIRE(visibleCatalog->nameMatches);
	REQUIRE(visibleCatalog->descriptionMatches);

	const auto* visibleMetadata = FindByName(report.metadataEntries, L"parity-visible");
	REQUIRE(visibleMetadata != nullptr);
	REQUIRE(visibleMetadata->openClawFrontmatterParsed);
	REQUIRE(visibleMetadata->catalogFrontmatterValid);
	REQUIRE(visibleMetadata->catalogMetadataPresent);

	const auto* visibleEligibility = FindByName(report.eligibilityEntries, L"parity-visible");
	REQUIRE(visibleEligibility != nullptr);
	REQUIRE(visibleEligibility->actualEligible);

	const auto* hiddenVisibility = FindByName(report.visibilityEntries, L"parity-hidden");
	REQUIRE(hiddenVisibility != nullptr);
	REQUIRE(hiddenVisibility->hiddenByDisableModelInvocation);
	REQUIRE_FALSE(hiddenVisibility->expectedVisible);
	REQUIRE_FALSE(hiddenVisibility->visibleInPrompt);

	const auto* sourcePrecedence = FindByName(report.precedenceEntries, L"parity-source");
	REQUIRE(sourcePrecedence != nullptr);
	REQUIRE(sourcePrecedence->deterministic);
	REQUIRE(sourcePrecedence->winnerSourceLabel == L"workspace");

	const auto* compactVisible = FindByName(report.compactFallbackEntries, L"parity-visible");
	REQUIRE(compactVisible != nullptr);
	REQUIRE(compactVisible->includedInFullPromptSnapshot);

	const auto markdown = service.BuildMarkdownReport(report);
	REQUIRE(markdown.find("Catalog snapshot parity (presence/name/description)") != std::string::npos);
	REQUIRE(markdown.find("Metadata parse parity") != std::string::npos);
	REQUIRE(markdown.find("Eligibility parity") != std::string::npos);
	REQUIRE(markdown.find("Prompt visibility / intentional hiding parity") != std::string::npos);
	REQUIRE(markdown.find("Compact fallback retention parity") != std::string::npos);
	REQUIRE(markdown.find("Source precedence parity") != std::string::npos);

	const auto json = service.BuildJsonReport(report);
	REQUIRE(json.find("\"catalogEntries\"") != std::string::npos);
	REQUIRE(json.find("\"metadataEntries\"") != std::string::npos);
	REQUIRE(json.find("\"eligibilityEntries\"") != std::string::npos);
	REQUIRE(json.find("\"visibilityEntries\"") != std::string::npos);
	REQUIRE(json.find("\"compactFallbackEntries\"") != std::string::npos);
	REQUIRE(json.find("\"precedenceEntries\"") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}
