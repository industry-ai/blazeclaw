#include "pch.h"
#include "SkillsFutureStabilityValidationService.h"

#include "SkillsCatalogService.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <unordered_set>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLower(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		std::string WideToUtf8(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return output;
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& pathValue) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(pathValue, ec);
			if (ec) {
				return pathValue.lexically_normal();
			}
			return canonical;
		}

		bool ContainsAnyToken(const std::wstring& text, const std::vector<std::wstring>& tokens) {
			const auto lowered = ToLower(text);
			return std::any_of(
				tokens.begin(),
				tokens.end(),
				[&lowered](const std::wstring& token) {
					return !token.empty() && lowered.find(token) != std::wstring::npos;
				});
		}

		bool HasFieldKey(
			const std::map<std::wstring, std::wstring>& fields,
			const std::unordered_set<std::wstring>& keys) {
			for (const auto& item : fields) {
				if (keys.contains(ToLower(Trim(item.first)))) {
					return true;
				}
			}
			return false;
		}

		bool HasFieldKeyPrefix(
			const std::map<std::wstring, std::wstring>& fields,
			const std::wstring& keyPrefix) {
			const auto normalizedPrefix = ToLower(Trim(keyPrefix));
			if (normalizedPrefix.empty()) {
				return false;
			}

			for (const auto& item : fields) {
				const auto normalizedKey = ToLower(Trim(item.first));
				if (normalizedKey.starts_with(normalizedPrefix)) {
					return true;
				}
			}

			return false;
		}

		bool IsProtectedFamilyInStableRoot(
			const std::filesystem::path& skillDir,
			const SkillsSourceKind sourceKind) {
			if (sourceKind == SkillsSourceKind::Bundled ||
				sourceKind == SkillsSourceKind::OpenClawOriginal) {
				return true;
			}

			const auto pathText = ToLower(skillDir.generic_wstring());
			return pathText.find(L"/skills-bundled/") != std::wstring::npos ||
				pathText.find(L"/skills-openclaw-original/") != std::wstring::npos;
		}

		SkillsFutureStabilityCompatibilityRow MakeRow(const SkillsCatalogEntry& entry) {
			SkillsFutureStabilityCompatibilityRow row;
			row.skillName = entry.skillName;
			row.skillDir = entry.skillDir;
			return row;
		}

		void AddViolation(
			std::vector<SkillsFutureStabilityViolationEntry>& bucket,
			SkillsFutureStabilityCompatibilityRow& row,
			SkillsFutureStabilityStatus SkillsFutureStabilityCompatibilityRow::* statusField,
			const std::wstring& policy,
			const std::wstring& reasonCode,
			const std::wstring& detail) {
			bucket.push_back(SkillsFutureStabilityViolationEntry{
				.skillName = row.skillName,
				.skillDir = row.skillDir,
				.policy = policy,
				.reasonCode = reasonCode,
				.detail = detail,
			});
			row.*statusField = SkillsFutureStabilityStatus::Violation;
			if (!row.notes.empty()) {
				row.notes += L"; ";
			}
			row.notes += reasonCode;
		}

	} // namespace

	std::wstring SkillsFutureStabilityValidationService::StatusLabel(
		const SkillsFutureStabilityStatus status) {
		switch (status) {
		case SkillsFutureStabilityStatus::Compliant:
			return L"compliant";
		case SkillsFutureStabilityStatus::Violation:
		default:
			return L"violation";
		}
	}

	SkillsFutureStabilityValidationReport SkillsFutureStabilityValidationService::BuildReport(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsFutureStabilityValidationReport report;
		report.workspaceRoot = CanonicalOrSelf(workspaceRoot);

		SkillsCatalogService catalogService;
		const auto catalog = catalogService.LoadCatalog(workspaceRoot, appConfig);

		for (const auto& warning : catalog.diagnostics.warnings) {
			report.warnings.push_back(warning);
		}

		const std::unordered_set<std::wstring> workflowAssumptionKeys{
			L"workflow",
			L"workflow-mode",
			L"workflow_mode",
			L"workflow-policy",
			L"workflow_policy",
			L"ordered-steps",
			L"ordered_steps",
			L"sequence-lock",
			L"sequence_lock",
		};
		const std::unordered_set<std::wstring> runtimeBoundaryKeys{
			L"runtime-adapter",
			L"runtime_adapter",
			L"runtime-config-key",
			L"runtime_config_key",
			L"blazeclaw-specific",
			L"blazeclaw_specific",
		};
		const std::unordered_set<std::wstring> renameFromKeys{
			L"renamed-from",
			L"renamed_from",
			L"migration-from",
			L"migration_from",
		};
		const std::unordered_set<std::wstring> renameSafetyKeys{
			L"compatibility-alias",
			L"compatibility_alias",
			L"managed-migration",
			L"managed_migration",
			L"migration-id",
			L"migration_id",
		};
		const std::set<std::wstring> protectedFamilies{
			L"self-evolving",
			L"imap-smtp-email",
			L"search",
			L"web-browsing",
		};

		for (const auto& entry : catalog.entries) {
			auto row = MakeRow(entry);

			const auto hasWorkflowStructuralMarker =
				HasFieldKey(entry.frontmatter.fields, workflowAssumptionKeys) ||
				HasFieldKeyPrefix(entry.frontmatter.fields, L"workflow.") ||
				HasFieldKeyPrefix(entry.frontmatter.fields, L"workflow-") ||
				ContainsAnyToken(entry.description, { L"workflow:", L"workflow=" });
			if (hasWorkflowStructuralMarker) {
				AddViolation(
					report.workflowAssumptionViolations,
					row,
					&SkillsFutureStabilityCompatibilityRow::workflowAssumptionStatus,
					L"workflow-assumption",
					L"workflow_specific_marker_detected",
					L"skill metadata encodes workflow-specific orchestration marker");
			}

			const auto hasRuntimeBoundaryMarker =
				HasFieldKey(entry.frontmatter.fields, runtimeBoundaryKeys) ||
				HasFieldKeyPrefix(entry.frontmatter.fields, L"blazeclaw.") ||
				HasFieldKeyPrefix(entry.frontmatter.fields, L"blazeclaw-") ||
				HasFieldKeyPrefix(entry.frontmatter.fields, L"runtime.");
			if (hasRuntimeBoundaryMarker) {
				AddViolation(
					report.runtimeBoundaryViolations,
					row,
					&SkillsFutureStabilityCompatibilityRow::runtimeBoundaryStatus,
					L"runtime-boundary",
					L"skill_file_runtime_specific_metadata",
					L"runtime-specific behavior should be moved to adapter/config");
			}

			const auto normalizedName = ToLower(Trim(entry.skillName));
			if (protectedFamilies.contains(normalizedName)) {
				const bool inStableDir = IsProtectedFamilyInStableRoot(
					entry.skillDir,
					entry.sourceKind);
				if (!inStableDir) {
					AddViolation(
						report.protectedDirectoryViolations,
						row,
						&SkillsFutureStabilityCompatibilityRow::protectedDirectoryStatus,
						L"stable-directory",
						L"protected_family_not_in_stable_dir",
						L"protected skill family is not located in stable skill roots");
				}
			}

			const bool hasRenameFrom = HasFieldKey(entry.frontmatter.fields, renameFromKeys);
			if (hasRenameFrom) {
				const bool hasAlias = HasFieldKey(entry.frontmatter.fields, renameSafetyKeys);
				if (!hasAlias) {
					AddViolation(
						report.renameSafetyViolations,
						row,
						&SkillsFutureStabilityCompatibilityRow::renameSafetyStatus,
						L"rename-safety",
						L"rename_without_alias_or_migration",
						L"renamed skill requires compatibility alias or managed migration metadata");
				}
			}

			if (!entry.validFrontmatter ||
				!entry.metadata.has_value() ||
				entry.skillName.empty()) {
				AddViolation(
					report.frontmatterCompatibilityViolations,
					row,
					&SkillsFutureStabilityCompatibilityRow::frontmatterCompatibilityStatus,
					L"frontmatter-compatibility",
					L"non_openclaw_compatible_frontmatter",
					L"skill frontmatter failed OpenClaw-compatible contract checks");
			}

			report.compatibilityMatrix.push_back(std::move(row));
		}

		auto bySkillThenPath = [](const auto& left, const auto& right) {
			const auto leftSkill = ToLower(Trim(left.skillName));
			const auto rightSkill = ToLower(Trim(right.skillName));
			if (leftSkill != rightSkill) {
				return leftSkill < rightSkill;
			}
			return left.skillDir.generic_wstring() < right.skillDir.generic_wstring();
		};

		std::sort(
			report.workflowAssumptionViolations.begin(),
			report.workflowAssumptionViolations.end(),
			bySkillThenPath);
		std::sort(
			report.runtimeBoundaryViolations.begin(),
			report.runtimeBoundaryViolations.end(),
			bySkillThenPath);
		std::sort(
			report.protectedDirectoryViolations.begin(),
			report.protectedDirectoryViolations.end(),
			bySkillThenPath);
		std::sort(
			report.renameSafetyViolations.begin(),
			report.renameSafetyViolations.end(),
			bySkillThenPath);
		std::sort(
			report.frontmatterCompatibilityViolations.begin(),
			report.frontmatterCompatibilityViolations.end(),
			bySkillThenPath);
		std::sort(
			report.compatibilityMatrix.begin(),
			report.compatibilityMatrix.end(),
			bySkillThenPath);

		return report;
	}

	std::string SkillsFutureStabilityValidationService::BuildMarkdownReport(
		const SkillsFutureStabilityValidationReport& report) const {
		std::ostringstream output;
		output << "# Skills Future Stability Validation Report (Phase 7)\n\n";
		output << "## Violation summary\n\n";
		output << "- workflowAssumptionViolations="
			<< report.workflowAssumptionViolations.size() << "\n";
		output << "- runtimeBoundaryViolations="
			<< report.runtimeBoundaryViolations.size() << "\n";
		output << "- protectedDirectoryViolations="
			<< report.protectedDirectoryViolations.size() << "\n";
		output << "- renameSafetyViolations="
			<< report.renameSafetyViolations.size() << "\n";
		output << "- frontmatterCompatibilityViolations="
			<< report.frontmatterCompatibilityViolations.size() << "\n\n";

		auto writeViolations = [&output](
			const std::string& title,
			const std::vector<SkillsFutureStabilityViolationEntry>& violations) {
			output << "## " << title << "\n\n";
			if (violations.empty()) {
				output << "- (none)\n\n";
				return;
			}
			for (const auto& entry : violations) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; reason=`" << WideToUtf8(entry.reasonCode) << "`"
					<< "; detail=" << WideToUtf8(entry.detail)
					<< "\n";
			}
			output << "\n";
		};

		writeViolations("Workflow-specific assumption violations", report.workflowAssumptionViolations);
		writeViolations("Runtime-adapter/config boundary violations", report.runtimeBoundaryViolations);
		writeViolations("Protected-directory stability violations", report.protectedDirectoryViolations);
		writeViolations("Rename safety violations", report.renameSafetyViolations);
		writeViolations("Frontmatter compatibility violations", report.frontmatterCompatibilityViolations);

		output << "## Compatibility matrix\n\n";
		if (report.compatibilityMatrix.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.compatibilityMatrix) {
				output << "- skill=`" << WideToUtf8(row.skillName) << "`"
					<< "; workflow=" << WideToUtf8(StatusLabel(row.workflowAssumptionStatus))
					<< "; boundary=" << WideToUtf8(StatusLabel(row.runtimeBoundaryStatus))
					<< "; stableDir=" << WideToUtf8(StatusLabel(row.protectedDirectoryStatus))
					<< "; rename=" << WideToUtf8(StatusLabel(row.renameSafetyStatus))
					<< "; frontmatter=" << WideToUtf8(StatusLabel(row.frontmatterCompatibilityStatus))
					<< "\n";
			}
			output << "\n";
		}

		output << "## Warnings\n\n";
		if (report.warnings.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& warning : report.warnings) {
				output << "- " << WideToUtf8(warning) << "\n";
			}
			output << "\n";
		}

		return output.str();
	}

	std::string SkillsFutureStabilityValidationService::BuildJsonReport(
		const SkillsFutureStabilityValidationReport& report) const {
		nlohmann::json root;
		root["workspaceRoot"] = report.workspaceRoot.generic_string();
		root["violationSummary"] = {
			{ "workflowAssumptionViolations", report.workflowAssumptionViolations.size() },
			{ "runtimeBoundaryViolations", report.runtimeBoundaryViolations.size() },
			{ "protectedDirectoryViolations", report.protectedDirectoryViolations.size() },
			{ "renameSafetyViolations", report.renameSafetyViolations.size() },
			{ "frontmatterCompatibilityViolations", report.frontmatterCompatibilityViolations.size() },
		};

		auto writeViolationArray = [](const auto& entries) {
			nlohmann::json array = nlohmann::json::array();
			for (const auto& entry : entries) {
				array.push_back({
					{ "skillName", WideToUtf8(entry.skillName) },
					{ "skillDir", entry.skillDir.generic_string() },
					{ "policy", WideToUtf8(entry.policy) },
					{ "reasonCode", WideToUtf8(entry.reasonCode) },
					{ "detail", WideToUtf8(entry.detail) },
				});
			}
			return array;
		};

		root["workflowAssumptionViolations"] = writeViolationArray(report.workflowAssumptionViolations);
		root["runtimeBoundaryViolations"] = writeViolationArray(report.runtimeBoundaryViolations);
		root["protectedDirectoryViolations"] = writeViolationArray(report.protectedDirectoryViolations);
		root["renameSafetyViolations"] = writeViolationArray(report.renameSafetyViolations);
		root["frontmatterCompatibilityViolations"] = writeViolationArray(report.frontmatterCompatibilityViolations);

		root["compatibilityMatrix"] = nlohmann::json::array();
		for (const auto& row : report.compatibilityMatrix) {
			root["compatibilityMatrix"].push_back({
				{ "skillName", WideToUtf8(row.skillName) },
				{ "skillDir", row.skillDir.generic_string() },
				{ "workflowAssumptionStatus", WideToUtf8(StatusLabel(row.workflowAssumptionStatus)) },
				{ "runtimeBoundaryStatus", WideToUtf8(StatusLabel(row.runtimeBoundaryStatus)) },
				{ "protectedDirectoryStatus", WideToUtf8(StatusLabel(row.protectedDirectoryStatus)) },
				{ "renameSafetyStatus", WideToUtf8(StatusLabel(row.renameSafetyStatus)) },
				{ "frontmatterCompatibilityStatus", WideToUtf8(StatusLabel(row.frontmatterCompatibilityStatus)) },
				{ "notes", WideToUtf8(row.notes) },
			});
		}

		root["warnings"] = nlohmann::json::array();
		for (const auto& warning : report.warnings) {
			root["warnings"].push_back(WideToUtf8(warning));
		}

		return root.dump(2);
	}

} // namespace blazeclaw::core
