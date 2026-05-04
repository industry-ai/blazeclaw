#include "pch.h"
#include "config/ConfigModels.h"
#include "core/SkillsCatalogService.h"
#include "core/SkillsEligibilityService.h"
#include "core/SkillsGatewayProjectionService.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <cstdlib>
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
			("blazeclaw_skills_browser_contract_" + suffix + "_" + std::to_string(std::rand()));
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

	const blazeclaw::core::SkillsEligibilityEntry* FindEligibility(
		const blazeclaw::core::SkillsEligibilitySnapshot& snapshot,
		const std::wstring& skillName) {
		const auto it = std::find_if(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[&skillName](const blazeclaw::core::SkillsEligibilityEntry& entry) {
				return entry.skillName == skillName;
			});
		return it == snapshot.entries.end() ? nullptr : &(*it);
	}

	const blazeclaw::core::SkillsCatalogEntry* FindCatalogEntry(
		const blazeclaw::core::SkillsCatalogSnapshot& snapshot,
		const std::wstring& skillName) {
		const auto it = std::find_if(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[&skillName](const blazeclaw::core::SkillsCatalogEntry& entry) {
				return entry.skillName == skillName;
			});
		return it == snapshot.entries.end() ? nullptr : &(*it);
	}

} // namespace

TEST_CASE("Skills gateway projection marks imported openclaw-original skills for browser discoverability", "[skills][browser][projection][openclaw-original]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
	const auto workspaceRoot = CreateWorkspaceRoot("openclaw_imported_projection");

	const auto skillDir =
		workspaceRoot /
		"blazeclaw" /
		"skills-openclaw-original" /
		"skill-creator-0.1.0";
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
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService catalogService;
	const auto catalog = catalogService.LoadCatalog(workspaceRoot, config);
	const auto* catalogEntry = FindCatalogEntry(catalog, L"skill-creator");
	REQUIRE(catalogEntry != nullptr);
	REQUIRE(catalogEntry->sourceKind == blazeclaw::core::SkillsSourceKind::OpenClawOriginal);

	blazeclaw::core::SkillsEligibilityService eligibilityService;
	const auto eligibility = eligibilityService.Evaluate(catalog, config);
	const auto* eligibilityEntry = FindEligibility(eligibility, L"skill-creator");
	REQUIRE(eligibilityEntry != nullptr);

	blazeclaw::core::SkillsGatewayProjectionService projectionService;
	const auto gatewayEntry = projectionService.BuildGatewaySkillEntry(
		*catalogEntry,
		eligibilityEntry,
		nullptr,
		nullptr);

	REQUIRE(gatewayEntry.browserGroup == "imported");
	REQUIRE(gatewayEntry.browserDisplayName.find("missing tool-manifest") == std::string::npos);
	REQUIRE(gatewayEntry.browserSourceLabel == "openclaw-original");
	REQUIRE(gatewayEntry.browserVariantLabel == "skill-creator-0.1.0");
	REQUIRE(gatewayEntry.browserDisplayName.find("skill-creator-0.1.0") != std::string::npos);

	std::filesystem::remove_all(workspaceRoot);
}

TEST_CASE("Skills gateway projection keeps duplicate-name variants distinguishable in browser labels", "[skills][browser][projection][duplicate-name]") {
	ScopedOpenClawOriginalDirOverride envOverrideGuard(
		L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
	const auto workspaceRoot = CreateWorkspaceRoot("duplicate_name_variants");

	const auto bundledSkillDir =
		workspaceRoot /
		"skills-bundled" /
		"skill-creator";
	WriteTextFile(
		bundledSkillDir / "SKILL.md",
		L"---\n"
		L"name: skill-creator\n"
		L"description: Bundled skill creator\n"
		L"---\n"
		L"# Skill Creator\n");

	const auto managedSkillDir =
		workspaceRoot /
		".blazeclaw" /
		"skills" /
		"skill-creator-managed";
	WriteTextFile(
		managedSkillDir / "SKILL.md",
		L"---\n"
		L"name: skill-creator-managed\n"
		L"description: Managed skill creator variant\n"
		L"---\n"
		L"# Skill Creator Managed\n");

	blazeclaw::config::AppConfig config;
	config.skills.openclawOriginal.enabled = true;
	config.skills.openclawOriginal.autoImportTools = true;
	config.skills.openclawOriginal.promoteToManaged = false;
	config.skills.limits.maxCandidatesPerRoot = 32;
	config.skills.limits.maxSkillsLoadedPerSource = 32;
	config.skills.limits.maxSkillFileBytes = 64 * 1024;

	blazeclaw::core::SkillsCatalogService catalogService;
	const auto catalog = catalogService.LoadCatalog(workspaceRoot, config);
	const auto* bundledEntry = FindCatalogEntry(catalog, L"skill-creator");
	const auto* managedEntry = FindCatalogEntry(catalog, L"skill-creator-managed");
	REQUIRE(bundledEntry != nullptr);
	REQUIRE(managedEntry != nullptr);

	blazeclaw::core::SkillsEligibilityService eligibilityService;
	const auto eligibility = eligibilityService.Evaluate(catalog, config);

	blazeclaw::core::SkillsGatewayProjectionService projectionService;
	const auto bundledGatewayEntry = projectionService.BuildGatewaySkillEntry(
		*bundledEntry,
		FindEligibility(eligibility, L"skill-creator"),
		nullptr,
		nullptr);
	const auto managedGatewayEntry = projectionService.BuildGatewaySkillEntry(
		*managedEntry,
		FindEligibility(eligibility, L"skill-creator-managed"),
		nullptr,
		nullptr);

	REQUIRE(bundledGatewayEntry.browserSourceLabel == "bundled");
	REQUIRE(bundledGatewayEntry.browserVariantLabel == "skill-creator");
	REQUIRE(bundledGatewayEntry.browserDisplayName == "skill-creator — bundled");
	REQUIRE(managedGatewayEntry.browserSourceLabel == "managed");
	REQUIRE(managedGatewayEntry.browserVariantLabel == "skill-creator-managed");
	REQUIRE(managedGatewayEntry.browserDisplayName == "skill-creator-managed — managed");

	std::filesystem::remove_all(workspaceRoot);
}
