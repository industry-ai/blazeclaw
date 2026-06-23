#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsInventoryValidationDriftEntry {
		std::wstring skillName;
		std::filesystem::path openClawSkillFile;
		std::filesystem::path blazeClawSkillFile;
		std::uint64_t openClawHash = 0;
		std::uint64_t blazeClawHash = 0;
	};

	struct SkillsInventoryValidationReachabilityEntry {
		std::wstring skillName;
		std::filesystem::path skillDir;
		std::wstring reason;
	};

	struct SkillsInventoryValidationFrontmatterIssue {
		std::filesystem::path skillFile;
		std::wstring issueType;
		std::wstring detail;
		std::uint64_t sizeBytes = 0;
	};

	struct SkillsInventoryValidationReport {
		std::filesystem::path openClawBundledRoot;
		std::filesystem::path blazeClawBundledRoot;
		std::filesystem::path extensionsRoot;
		std::vector<std::filesystem::path> pluginRoots;
		bool openClawBundledRootFound = false;
		bool blazeClawBundledRootFound = false;
		bool extensionsRootFound = false;
		std::vector<std::wstring> missingOpenClawBundledSkills;
		std::vector<std::wstring> extraBlazeClawBundledSkills;
		std::vector<SkillsInventoryValidationDriftEntry> driftedCommonSkills;
		std::vector<SkillsInventoryValidationReachabilityEntry> unreachableExtensionSkills;
		std::vector<SkillsInventoryValidationFrontmatterIssue> frontmatterIssues;
		std::vector<std::wstring> warnings;
	};

	class SkillsInventoryValidationService {
	public:
		[[nodiscard]] SkillsInventoryValidationReport BuildReport(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] std::string BuildMarkdownReport(
			const SkillsInventoryValidationReport& report) const;

		[[nodiscard]] std::string BuildJsonReport(
			const SkillsInventoryValidationReport& report) const;
	};

} // namespace blazeclaw::core
