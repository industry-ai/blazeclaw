#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsCatalogPromptParityCatalogEntry {
		std::wstring skillName;
		bool catalogPresent = false;
		std::wstring expectedName;
		std::wstring catalogName;
		bool nameMatches = false;
		std::wstring expectedDescription;
		std::wstring catalogDescription;
		bool descriptionMatches = false;
		std::wstring sourceLabel;
	};

	struct SkillsCatalogPromptParityMetadataEntry {
		std::wstring skillName;
		bool openClawFrontmatterParsed = false;
		bool catalogFrontmatterValid = false;
		bool catalogMetadataPresent = false;
		std::vector<std::wstring> issues;
	};

	struct SkillsCatalogPromptParityEligibilityEntry {
		std::wstring skillName;
		bool expectedEligible = true;
		bool actualEligible = false;
		bool disabled = false;
		bool blockedByAllowlist = false;
		std::vector<std::wstring> issues;
	};

	struct SkillsCatalogPromptParityVisibilityEntry {
		std::wstring skillName;
		bool expectedVisible = true;
		bool visibleInPrompt = false;
		bool hiddenByDisableModelInvocation = false;
		std::vector<std::wstring> issues;
	};

	struct SkillsCatalogPromptParityCompactFallbackEntry {
		std::wstring skillName;
		bool includedInFullPromptSnapshot = false;
		bool retainedInCompactFallbackSnapshot = false;
		std::vector<std::wstring> issues;
	};

	struct SkillsCatalogPromptParityPrecedenceEntry {
		std::wstring skillName;
		std::wstring winnerSourceLabel;
		std::vector<std::wstring> candidateSourceLabels;
		bool deterministic = true;
		std::vector<std::wstring> issues;
	};

	struct SkillsCatalogPromptParityValidationReport {
		std::filesystem::path workspaceRoot;
		std::filesystem::path openClawBundledRoot;
		std::filesystem::path blazeClawBundledRoot;
		bool openClawBundledRootFound = false;
		bool blazeClawBundledRootFound = false;
		std::vector<SkillsCatalogPromptParityCatalogEntry> catalogEntries;
		std::vector<SkillsCatalogPromptParityMetadataEntry> metadataEntries;
		std::vector<SkillsCatalogPromptParityEligibilityEntry> eligibilityEntries;
		std::vector<SkillsCatalogPromptParityVisibilityEntry> visibilityEntries;
		std::vector<SkillsCatalogPromptParityCompactFallbackEntry> compactFallbackEntries;
		std::vector<SkillsCatalogPromptParityPrecedenceEntry> precedenceEntries;
		std::vector<std::wstring> warnings;
	};

	class SkillsCatalogPromptParityValidationService {
	public:
		[[nodiscard]] SkillsCatalogPromptParityValidationReport BuildReport(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] std::string BuildMarkdownReport(
			const SkillsCatalogPromptParityValidationReport& report) const;

		[[nodiscard]] std::string BuildJsonReport(
			const SkillsCatalogPromptParityValidationReport& report) const;
	};

} // namespace blazeclaw::core
