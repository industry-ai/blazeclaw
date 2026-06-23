#include "pch.h"

#include "config/ConfigModels.h"
#include "core/SkillsInventoryValidationService.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

	std::filesystem::path MakeTempWorkspace(const std::string& suffix) {
		const auto root = std::filesystem::temp_directory_path() /
			("blazeclaw_skills_inventory_validation_" + suffix + "_" + std::to_string(std::rand()));
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteUtf8File(const std::filesystem::path& filePath, const std::string& content) {
		std::filesystem::create_directories(filePath.parent_path());
		std::ofstream output(filePath, std::ios::binary);
		REQUIRE(output.is_open());
		output.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	std::string BuildValidSkill(const std::string& name, const std::string& description, const std::string& body) {
		return
			"---\n"
			"name: " + name + "\n"
			"description: " + description + "\n"
			"---\n" +
			body + "\n";
	}

	class ScopedEnvVar {
	public:
		explicit ScopedEnvVar(const wchar_t* name)
			: m_name(name == nullptr ? L"" : name) {
			if (m_name.empty()) {
				return;
			}

			wchar_t* value = nullptr;
			size_t length = 0;
			if (_wdupenv_s(&value, &length, m_name.c_str()) == 0 && value != nullptr) {
				m_hasOriginal = true;
				m_original = value;
				free(value);
			}
		}

		~ScopedEnvVar() {
			if (m_name.empty()) {
				return;
			}

			if (m_hasOriginal) {
				_wputenv_s(m_name.c_str(), m_original.c_str());
			}
			else {
				_wputenv_s(m_name.c_str(), L"");
			}
		}

		void Set(const std::wstring& value) const {
			if (m_name.empty()) {
				return;
			}
			_wputenv_s(m_name.c_str(), value.c_str());
		}

	private:
		std::wstring m_name;
		bool m_hasOriginal = false;
		std::wstring m_original;
	};

	bool ContainsSkillName(
		const std::vector<std::wstring>& values,
		const std::wstring& expected) {
		return std::find(values.begin(), values.end(), expected) != values.end();
	}

} // namespace

TEST_CASE("Phase 3 inventory validation reports missing/extra/drift/reachability/frontmatter issues", "[skills][inventory][phase3]") {
	const auto workspaceRoot = MakeTempWorkspace("full_report");

	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "common" / "SKILL.md",
		BuildValidSkill("common", "OpenClaw common", "# Common"));
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "missing-in-blaze" / "SKILL.md",
		BuildValidSkill("missing-in-blaze", "Only in OpenClaw", "# Missing"));
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "drifted" / "SKILL.md",
		BuildValidSkill("drifted", "OpenClaw drift", "LineA\nLineB"));
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "invalid-frontmatter" / "SKILL.md",
		"---\nname: invalid-frontmatter\ndescription: bad frontmatter\n");
	WriteUtf8File(
		workspaceRoot / "openclaw" / "skills" / "oversized" / "SKILL.md",
		BuildValidSkill("oversized", "Large frontmatter candidate", std::string(512, 'X')));

	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "common" / "SKILL.md",
		BuildValidSkill("common", "BlazeClaw common", "# Common"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "extra-in-blaze" / "SKILL.md",
		BuildValidSkill("extra-in-blaze", "Only in BlazeClaw", "# Extra"));
	WriteUtf8File(
		workspaceRoot / "skills-bundled" / "drifted" / "SKILL.md",
		BuildValidSkill("drifted", "BlazeClaw drift", "LineA\r\nLineC"));

	WriteUtf8File(
		workspaceRoot / "extensions" / "inventory-extension" / "SKILL.md",
		BuildValidSkill("inventory-extension", "Extension skill", "# Extension"));
	std::filesystem::create_directories(workspaceRoot / "plugin-skills");

	ScopedEnvVar pluginRoots(L"BLAZECLAW_PLUGIN_SKILL_DIRS");
	pluginRoots.Set((workspaceRoot / "plugin-skills").wstring());

	blazeclaw::config::AppConfig appConfig;
	appConfig.skills.load.rejectPathSymlink = true;
	appConfig.skills.limits.maxSkillFileBytes = 240;

	blazeclaw::core::SkillsInventoryValidationService service;
	const auto report = service.BuildReport(workspaceRoot, appConfig);

	REQUIRE(report.openClawBundledRootFound);
	REQUIRE(report.blazeClawBundledRootFound);
	REQUIRE(report.extensionsRootFound);
	REQUIRE(report.pluginRoots.size() == 1);

	REQUIRE(ContainsSkillName(report.missingOpenClawBundledSkills, L"missing-in-blaze"));
	REQUIRE(ContainsSkillName(report.extraBlazeClawBundledSkills, L"extra-in-blaze"));

	const auto drifted = std::find_if(
		report.driftedCommonSkills.begin(),
		report.driftedCommonSkills.end(),
		[](const blazeclaw::core::SkillsInventoryValidationDriftEntry& entry) {
			return entry.skillName == L"drifted";
		});
	REQUIRE(drifted != report.driftedCommonSkills.end());

	const auto unreachable = std::find_if(
		report.unreachableExtensionSkills.begin(),
		report.unreachableExtensionSkills.end(),
		[](const blazeclaw::core::SkillsInventoryValidationReachabilityEntry& entry) {
			return entry.skillName == L"inventory-extension";
		});
	REQUIRE(unreachable != report.unreachableExtensionSkills.end());

	const auto hasInvalidFrontmatter = std::any_of(
		report.frontmatterIssues.begin(),
		report.frontmatterIssues.end(),
		[](const blazeclaw::core::SkillsInventoryValidationFrontmatterIssue& issue) {
			return issue.issueType == L"invalid-frontmatter";
		});
	REQUIRE(hasInvalidFrontmatter);

	const auto hasOversized = std::any_of(
		report.frontmatterIssues.begin(),
		report.frontmatterIssues.end(),
		[](const blazeclaw::core::SkillsInventoryValidationFrontmatterIssue& issue) {
			return issue.issueType == L"oversized-frontmatter";
		});
	REQUIRE(hasOversized);

	const auto markdown = service.BuildMarkdownReport(report);
	REQUIRE(markdown.find("OpenClaw bundled skills missing from BlazeClaw") != std::string::npos);
	REQUIRE(markdown.find("BlazeClaw bundled skills absent from OpenClaw") != std::string::npos);
	REQUIRE(markdown.find("Common skills drift (`SKILL.md`, newline-normalized)") != std::string::npos);
	REQUIRE(markdown.find("Extension skills not reachable through BlazeClaw plugin roots") != std::string::npos);
	REQUIRE(markdown.find("Invalid or oversized frontmatter files") != std::string::npos);

	const auto json = service.BuildJsonReport(report);
	REQUIRE(json.find("\"missingOpenClawBundledSkills\"") != std::string::npos);
	REQUIRE(json.find("\"extraBlazeClawBundledSkills\"") != std::string::npos);
	REQUIRE(json.find("\"driftedCommonSkills\"") != std::string::npos);
	REQUIRE(json.find("\"unreachableExtensionSkills\"") != std::string::npos);
	REQUIRE(json.find("\"frontmatterIssues\"") != std::string::npos);

	std::error_code ec;
	std::filesystem::remove_all(workspaceRoot, ec);
}
