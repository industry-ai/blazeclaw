#include "config/ConfigModels.h"
#include "core/SkillsCatalogService.h"

#include <catch2/catch_all.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

	std::string WideToUtf8(const std::wstring& value) {
		if (value.empty()) {
			return {};
		}

		const int required = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (required <= 0) {
			return {};
		}

		std::string output(static_cast<std::size_t>(required), '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			required,
			nullptr,
			nullptr);
		return output;
	}

	void WriteTextFile(const std::filesystem::path& path, const std::wstring& content) {
		std::filesystem::create_directories(path.parent_path());
		std::ofstream output(path, std::ios::binary);
		REQUIRE(output.is_open());
		const std::string utf8 = WideToUtf8(content);
		output.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
	}

	std::filesystem::path CreateWorkspaceRoot(const std::string& suffix) {
		const auto root = std::filesystem::temp_directory_path() /
			("blazeclaw_openclaw_original_import_" + suffix + "_" + std::to_string(std::rand()));
		std::filesystem::create_directories(root);
		return root;
	}

	class ScopedOpenClawOriginalDirOverride {
	public:
		explicit ScopedOpenClawOriginalDirOverride(const wchar_t* variableName)
			: m_variableName(variableName == nullptr ? L"" : variableName) {
			if (m_variableName.empty()) {
				return;
			}

			wchar_t* current = nullptr;
			size_t length = 0;
			if (_wdupenv_s(&current, &length, m_variableName.c_str()) == 0 &&
				current != nullptr) {
				m_hadOriginalValue = true;
				m_originalValue = current;
				free(current);
			}

			_wputenv_s(m_variableName.c_str(), L"");
		}

		~ScopedOpenClawOriginalDirOverride() {
			if (m_variableName.empty()) {
				return;
			}

			if (m_hadOriginalValue) {
				_wputenv_s(m_variableName.c_str(), m_originalValue.c_str());
			}
			else {
				_wputenv_s(m_variableName.c_str(), L"");
			}
		}

	private:
		std::wstring m_variableName;
		bool m_hadOriginalValue = false;
		std::wstring m_originalValue;
	};

} // namespace

TEST_CASE("SkillsCatalogService imports openclaw-original metadata and activation state", "[skills][catalog][openclaw-original][import]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
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
		L"metadata: {\"clawdbot\":{\"emoji\":\":page_facing_up:\",\"requires\":{\"bins\":[\"nano-pdf\"]}}}\n"
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
	if (entryIt->openClawOriginalActivationState.value() !=
		blazeclaw::core::SkillsOpenClawOriginalActivationState::ToolEnabled) {
		INFO("activation diagnostics begin");
		for (const auto& diagnostic : entryIt->openClawOriginalImportDiagnostics) {
			INFO(std::string("diag: ") + WideToUtf8(diagnostic));
		}
	}
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::ToolEnabled);
	REQUIRE(entryIt->openClawOriginalMetadataConvertedFromClawdbot);
	REQUIRE(entryIt->openClawOriginalOrigin == L"openclaw-original");
	REQUIRE_FALSE(entryIt->openClawOriginalPromotedDir.empty());
	REQUIRE(std::filesystem::exists(entryIt->openClawOriginalPromotedDir / "SKILL.md"));
	REQUIRE(entryIt->metadata.has_value());

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("SkillsCatalogService reports failed activation for malformed openclaw-original SKILL", "[skills][catalog][openclaw-original][failed]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
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
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
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
		L"metadata: {\"openclaw\":{\"emoji\":\":octopus:\"}}\n"
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
	REQUIRE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(L"missing tool manifest") != std::wstring::npos;
			}));

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("SkillsCatalogService imports manifestless h5-ppt style skill as non-fatal discovered entry", "[skills][catalog][openclaw-original][fixture][h5-ppt]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
	const auto workspaceRoot = CreateWorkspaceRoot("h5_ppt_fixture");

	const auto skillDir =
		workspaceRoot /
		"blazeclaw" /
		"skills-openclaw-original" /
		"h5-ppt";
	WriteTextFile(
		skillDir / "SKILL.md",
		L"---\n"
		L"name: h5-ppt\n"
		L"description: Return fixed URL for h5-ppt intents.\n"
		L"tags: h5-ppt\n"
		L"---\n"
		L"# 炎图科技PPT\n"
		L"\n"
		L"Trigger scenarios:\n"
		L"- 路演h5\n"
		L"- 打开路演h5\n"
		L"\n"
		L"Output:\n"
		L"```json\n"
		L"{\"outputs\":[{\"type\":\"webview\",\"title\":\"炎图科技PPT\",\"url\":\"https://static.blazegraph.site/h5-ppt/index.html\"}]}\n"
		L"```\n");

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
			return entry.skillName == L"h5-ppt" &&
				entry.sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal;
		});
	REQUIRE(entryIt != snapshot.entries.end());
	REQUIRE(entryIt->validFrontmatter);
	REQUIRE(entryIt->openClawOriginalActivationState.has_value());
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::ToolEnabled);
	REQUIRE(entryIt->metadata.has_value());
	REQUIRE(entryIt->openClawOriginalExtractedRuntimeContract.has_value());
	REQUIRE(
		entryIt->openClawOriginalExtractedRuntimeContract->skillKey ==
		L"h5-ppt");
	REQUIRE_FALSE(
		entryIt->openClawOriginalExtractedRuntimeContract->triggerHints.empty());
	REQUIRE(
		std::find(
			entryIt->openClawOriginalExtractedRuntimeContract->triggerHints.begin(),
			entryIt->openClawOriginalExtractedRuntimeContract->triggerHints.end(),
			std::wstring(L"路演h5")) !=
		entryIt->openClawOriginalExtractedRuntimeContract->triggerHints.end());
	REQUIRE(
		entryIt->openClawOriginalExtractedRuntimeContract->output.has_value());
	REQUIRE(
		entryIt->openClawOriginalExtractedRuntimeContract->output->kind ==
		L"webview");
	REQUIRE(
		entryIt->openClawOriginalExtractedRuntimeContract->output->title ==
		L"炎图科技PPT");
	REQUIRE(
		entryIt->openClawOriginalExtractedRuntimeContract->output->url ==
		L"https://static.blazegraph.site/h5-ppt/index.html");
	REQUIRE(entryIt->openClawOriginalExtractedRuntimeContract->complete);
	REQUIRE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(
					L"tool-enabled via generated manifestless runtime contract") !=
					std::wstring::npos;
			}));
	REQUIRE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(L"missing tool manifest") != std::wstring::npos;
			}));
	REQUIRE_FALSE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(L"malformed metadata") != std::wstring::npos;
			}));

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("SkillsCatalogService discovers configured sourceDir skill-creator-0.1.0", "[skills][catalog][openclaw-original][source-dir]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
	const auto workspaceRoot = CreateWorkspaceRoot("skill_creator_source_dir");

	const auto configuredRoot = workspaceRoot / "custom-openclaw-skills";
	const auto skillDir = configuredRoot / "skill-creator-0.1.0";
	WriteTextFile(
		skillDir / "SKILL.md",
		L"---\n"
		L"name: skill-creator\n"
		L"description: Guide for creating effective skills\n"
		L"metadata: {\"openclaw\":{\"emoji\":\":hammer_and_wrench:\"}}\n"
		L"---\n"
		L"# Skill Creator\n");

	blazeclaw::config::AppConfig config;
	config.skills.openclawOriginal.enabled = true;
	config.skills.openclawOriginal.autoImportTools = true;
	config.skills.openclawOriginal.promoteToManaged = false;
	config.skills.openclawOriginal.sourceDir = L"custom-openclaw-skills";
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService service;
	const auto snapshot = service.LoadCatalog(workspaceRoot, config);

	const auto entryIt = std::find_if(
		snapshot.entries.begin(),
		snapshot.entries.end(),
		[](const blazeclaw::core::SkillsCatalogEntry& entry) {
			return entry.skillName == L"skill-creator" &&
				entry.sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal;
		});
	REQUIRE(entryIt != snapshot.entries.end());
	REQUIRE(entryIt->openClawOriginalActivationState.has_value());
	REQUIRE(
		entryIt->openClawOriginalActivationState.value() ==
		blazeclaw::core::SkillsOpenClawOriginalActivationState::Imported);
	REQUIRE(
		std::any_of(
			entryIt->openClawOriginalImportDiagnostics.begin(),
			entryIt->openClawOriginalImportDiagnostics.end(),
			[](const std::wstring& diagnostic) {
				return diagnostic.find(L"missing tool manifest") != std::wstring::npos;
			}));

	std::filesystem::remove_all(workspaceRoot);
}
