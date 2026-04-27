#include "config/ConfigModels.h"
#include "core/SkillsCatalogService.h"

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>

namespace {

	void WriteTextFile(const std::filesystem::path& path, const std::wstring& content) {
		std::filesystem::create_directories(path.parent_path());
		std::wofstream output(path);
		REQUIRE(output.is_open());
		output << content;
	}

	std::filesystem::path CreateWorkspaceRoot(const std::string& suffix) {
		const auto root = std::filesystem::temp_directory_path() /
			("blazeclaw_openclaw_original_import_" + suffix + "_" + std::to_string(std::rand()));
		std::filesystem::create_directories(root);
		return root;
	}

} // namespace

TEST_CASE("SkillsCatalogService imports openclaw-original metadata and activation state", "[skills][catalog][openclaw-original][import]") {
	const auto workspaceRoot = CreateWorkspaceRoot("tool_enabled");

	const auto skillDir =
		workspaceRoot /
		"blazeclaw" /
		"skills-openclaw-original" /
		"nano-pdf";
	WriteTextFile(
		skillDir / "SKILL.md",
		L"---\n"
		L"name: nano-pdf\n"
		L"description: Edit PDF pages\n"
		L"metadata: {\"clawdbot\":{\"emoji\":\"📄\",\"requires\":{\"bins\":[\"nano-pdf\"]}}}\n"
		L"---\n"
		L"# nano-pdf\n");
	WriteTextFile(
		skillDir / "tool-manifest.json",
		L"{\"schemaVersion\":1,\"tools\":[{\"id\":\"nano_pdf.edit\"}]}\n");

	blazeclaw::config::AppConfig config;
	config.skills.openclawOriginal.enabled = true;
	config.skills.openclawOriginal.autoImportTools = true;
	config.skills.openclawOriginal.promoteToManaged = true;
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService service;
	const auto snapshot = service.LoadCatalog(workspaceRoot, config);

	const auto entryIt = std::find_if(
		snapshot.entries.begin(),
		snapshot.entries.end(),
		[](const blazeclaw::core::SkillsCatalogEntry& entry) {
			return entry.skillName == L"nano-pdf" &&
				entry.sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal;
		});
	REQUIRE(entryIt != snapshot.entries.end());
	REQUIRE(entryIt->openClawOriginalActivationState.has_value());
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::ToolEnabled);
	REQUIRE(entryIt->openClawOriginalMetadataConvertedFromClawdbot);
	REQUIRE(entryIt->openClawOriginalOrigin == L"openclaw-original");
	REQUIRE_FALSE(entryIt->openClawOriginalPromotedDir.empty());
	REQUIRE(std::filesystem::exists(entryIt->openClawOriginalPromotedDir / "SKILL.md"));
	REQUIRE(entryIt->metadata.has_value());
	REQUIRE(entryIt->metadata->emoji == L"📄");

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("SkillsCatalogService reports failed activation for malformed openclaw-original SKILL", "[skills][catalog][openclaw-original][failed]") {
	const auto workspaceRoot = CreateWorkspaceRoot("failed");

	const auto skillDir =
		workspaceRoot /
		"blazeclaw" /
		"skills-openclaw-original" /
		"broken-skill";
	WriteTextFile(
		skillDir / "SKILL.md",
		L"---\n"
		L"description: missing name field\n"
		L"---\n"
		L"# broken\n");

	blazeclaw::config::AppConfig config;
	config.skills.openclawOriginal.enabled = true;
	config.skills.openclawOriginal.autoImportTools = true;
	config.skills.openclawOriginal.promoteToManaged = false;
	config.skills.load.strictFrontmatter = false;
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService service;
	const auto snapshot = service.LoadCatalog(workspaceRoot, config);

	const auto entryIt = std::find_if(
		snapshot.entries.begin(),
		snapshot.entries.end(),
		[](const blazeclaw::core::SkillsCatalogEntry& entry) {
			return entry.skillName == L"broken-skill" &&
				entry.sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal;
		});
	REQUIRE(entryIt != snapshot.entries.end());
	REQUIRE_FALSE(entryIt->validFrontmatter);
	REQUIRE(entryIt->openClawOriginalActivationState.has_value());
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::Failed);
	REQUIRE_FALSE(entryIt->openClawOriginalImportDiagnostics.empty());

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("SkillsCatalogService keeps imported state when manifest missing", "[skills][catalog][openclaw-original][imported]") {
	const auto workspaceRoot = CreateWorkspaceRoot("imported");

	const auto skillDir =
		workspaceRoot /
		"blazeclaw" /
		"skills-openclaw-original" /
		"github";
	WriteTextFile(
		skillDir / "SKILL.md",
		L"---\n"
		L"name: github\n"
		L"description: GitHub CLI helper\n"
		L"metadata: {\"openclaw\":{\"emoji\":\"🐙\"}}\n"
		L"---\n"
		L"# github\n");

	blazeclaw::config::AppConfig config;
	config.skills.openclawOriginal.enabled = true;
	config.skills.openclawOriginal.autoImportTools = true;
	config.skills.openclawOriginal.promoteToManaged = false;
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService service;
	const auto snapshot = service.LoadCatalog(workspaceRoot, config);

	const auto entryIt = std::find_if(
		snapshot.entries.begin(),
		snapshot.entries.end(),
		[](const blazeclaw::core::SkillsCatalogEntry& entry) {
			return entry.skillName == L"github" &&
				entry.sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal;
		});
	REQUIRE(entryIt != snapshot.entries.end());
	REQUIRE(entryIt->openClawOriginalActivationState.has_value());
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::Imported);
	REQUIRE(entryIt->metadata.has_value());
	REQUIRE(entryIt->metadata->emoji == L"🐙");
	REQUIRE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(L"missing tool manifest") != std::wstring::npos;
			}));

	std::filesystem::remove_all(workspaceRoot);
}
