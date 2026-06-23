#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	enum class SkillsFutureStabilityStatus {
		Compliant = 0,
		Violation = 1,
	};

	struct SkillsFutureStabilityViolationEntry {
		std::wstring skillName;
		std::filesystem::path skillDir;
		std::wstring policy;
		std::wstring reasonCode;
		std::wstring detail;
	};

	struct SkillsFutureStabilityCompatibilityRow {
		std::wstring skillName;
		std::filesystem::path skillDir;
		SkillsFutureStabilityStatus workflowAssumptionStatus = SkillsFutureStabilityStatus::Compliant;
		SkillsFutureStabilityStatus runtimeBoundaryStatus = SkillsFutureStabilityStatus::Compliant;
		SkillsFutureStabilityStatus protectedDirectoryStatus = SkillsFutureStabilityStatus::Compliant;
		SkillsFutureStabilityStatus renameSafetyStatus = SkillsFutureStabilityStatus::Compliant;
		SkillsFutureStabilityStatus frontmatterCompatibilityStatus = SkillsFutureStabilityStatus::Compliant;
		std::wstring notes;
	};

	struct SkillsFutureStabilityValidationReport {
		std::filesystem::path workspaceRoot;
		std::vector<SkillsFutureStabilityViolationEntry> workflowAssumptionViolations;
		std::vector<SkillsFutureStabilityViolationEntry> runtimeBoundaryViolations;
		std::vector<SkillsFutureStabilityViolationEntry> protectedDirectoryViolations;
		std::vector<SkillsFutureStabilityViolationEntry> renameSafetyViolations;
		std::vector<SkillsFutureStabilityViolationEntry> frontmatterCompatibilityViolations;
		std::vector<SkillsFutureStabilityCompatibilityRow> compatibilityMatrix;
		std::vector<std::wstring> warnings;
	};

	class SkillsFutureStabilityValidationService {
	public:
		[[nodiscard]] SkillsFutureStabilityValidationReport BuildReport(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] std::string BuildMarkdownReport(
			const SkillsFutureStabilityValidationReport& report) const;

		[[nodiscard]] std::string BuildJsonReport(
			const SkillsFutureStabilityValidationReport& report) const;

		[[nodiscard]] static std::wstring StatusLabel(SkillsFutureStabilityStatus status);
	};

} // namespace blazeclaw::core
