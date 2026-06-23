#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	enum class SkillsExecutionReadinessMode {
		HostLocal = 0,
		RemoteCapable = 1,
		Blocked = 2,
	};

	enum class SkillsExecutionReadinessSmokeStatus {
		Passed = 0,
		Failed = 1,
		Skipped = 2,
	};

	struct SkillsExecutionDependencyEntry {
		std::wstring familyKey;
		std::vector<std::wstring> requiredBins;
		std::vector<std::wstring> requiredEnv;
		std::vector<std::wstring> requiredConfig;
		std::vector<std::wstring> missingBins;
		std::vector<std::wstring> missingEnv;
		std::vector<std::wstring> missingConfig;
		bool ready = false;
	};

	struct SkillsExecutionModeEntry {
		std::wstring familyKey;
		SkillsExecutionReadinessMode mode = SkillsExecutionReadinessMode::Blocked;
		std::wstring evidence;
	};

	struct SkillsExecutionToolMappingEntry {
		std::wstring familyKey;
		std::wstring expectedOpenClawTool;
		std::wstring blazeClawToolOrAdapter;
		bool mapped = false;
		std::wstring detail;
	};

	struct SkillsExecutionSmokeEntry {
		std::wstring familyKey;
		bool gatedByDependencies = true;
		SkillsExecutionReadinessSmokeStatus status = SkillsExecutionReadinessSmokeStatus::Skipped;
		std::wstring detail;
	};

	struct SkillsExecutionCompatibilityMatrixRow {
		std::wstring familyKey;
		SkillsExecutionReadinessMode mode = SkillsExecutionReadinessMode::Blocked;
		bool dependenciesReady = false;
		bool toolMappingReady = false;
		SkillsExecutionReadinessSmokeStatus smokeStatus = SkillsExecutionReadinessSmokeStatus::Skipped;
		std::wstring notes;
	};

	struct SkillsExecutionReadinessValidationReport {
		std::filesystem::path workspaceRoot;
		std::vector<SkillsExecutionDependencyEntry> dependencyEntries;
		std::vector<SkillsExecutionModeEntry> modeEntries;
		std::vector<SkillsExecutionToolMappingEntry> toolMappingEntries;
		std::vector<SkillsExecutionSmokeEntry> smokeEntries;
		std::vector<SkillsExecutionCompatibilityMatrixRow> compatibilityMatrix;
		std::vector<std::wstring> warnings;
	};

	class SkillsExecutionReadinessValidationService {
	public:
		[[nodiscard]] SkillsExecutionReadinessValidationReport BuildReport(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] std::string BuildMarkdownReport(
			const SkillsExecutionReadinessValidationReport& report) const;

		[[nodiscard]] std::string BuildJsonReport(
			const SkillsExecutionReadinessValidationReport& report) const;

		[[nodiscard]] static std::wstring ReadinessModeLabel(SkillsExecutionReadinessMode mode);
		[[nodiscard]] static std::wstring SmokeStatusLabel(SkillsExecutionReadinessSmokeStatus status);
	};

} // namespace blazeclaw::core
