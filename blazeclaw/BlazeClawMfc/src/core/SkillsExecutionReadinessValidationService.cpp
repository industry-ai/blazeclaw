#include "pch.h"
#include "SkillsExecutionReadinessValidationService.h"

#include "SkillsCatalogService.h"
#include "SkillsCommandService.h"
#include "SkillsEligibilityService.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <unordered_map>

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

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& value) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(value, ec);
			if (ec) {
				return value.lexically_normal();
			}
			return canonical;
		}

		template<typename TValue>
		void SortUniqueInPlace(std::vector<TValue>& values) {
			std::sort(values.begin(), values.end());
			values.erase(std::unique(values.begin(), values.end()), values.end());
		}

		std::vector<std::wstring> NormalizeRequiredList(const std::vector<std::wstring>& input) {
			std::vector<std::wstring> normalized;
			normalized.reserve(input.size());
			for (const auto& item : input) {
				const auto value = ToLower(Trim(item));
				if (!value.empty()) {
					normalized.push_back(value);
				}
			}
			SortUniqueInPlace(normalized);
			return normalized;
		}

		bool ContainsIgnoreCase(
			const std::vector<std::wstring>& values,
			const std::wstring& target) {
			const auto normalizedTarget = ToLower(Trim(target));
			return std::find_if(
				values.begin(),
				values.end(),
				[&normalizedTarget](const std::wstring& item) {
					return ToLower(Trim(item)) == normalizedTarget;
				}) != values.end();
		}

	} // namespace

	std::wstring SkillsExecutionReadinessValidationService::ReadinessModeLabel(
		const SkillsExecutionReadinessMode mode) {
		switch (mode) {
		case SkillsExecutionReadinessMode::HostLocal:
			return L"host-local";
		case SkillsExecutionReadinessMode::RemoteCapable:
			return L"remote-capable";
		case SkillsExecutionReadinessMode::Blocked:
		default:
			return L"blocked";
		}
	}

	std::wstring SkillsExecutionReadinessValidationService::SmokeStatusLabel(
		const SkillsExecutionReadinessSmokeStatus status) {
		switch (status) {
		case SkillsExecutionReadinessSmokeStatus::Passed:
			return L"passed";
		case SkillsExecutionReadinessSmokeStatus::Failed:
			return L"failed";
		case SkillsExecutionReadinessSmokeStatus::Skipped:
		default:
			return L"skipped";
		}
	}

	SkillsExecutionReadinessValidationReport SkillsExecutionReadinessValidationService::BuildReport(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsExecutionReadinessValidationReport report;
		report.workspaceRoot = CanonicalOrSelf(workspaceRoot);

		SkillsCatalogService catalogService;
		const auto catalog = catalogService.LoadCatalog(workspaceRoot, appConfig);
		SkillsEligibilityService eligibilityService;
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		SkillsCommandService commandService;
		const auto commandSnapshot = commandService.BuildSnapshot(catalog, eligibility, { L"skill" });

		std::unordered_map<std::wstring, const SkillsEligibilityEntry*> eligibilityBySkill;
		for (const auto& entry : eligibility.entries) {
			eligibilityBySkill.insert_or_assign(ToLower(Trim(entry.skillName)), &entry);
		}

		std::unordered_map<std::wstring, std::vector<const SkillsCommandSpec*>> commandsBySkill;
		for (const auto& command : commandSnapshot.commands) {
			commandsBySkill[ToLower(Trim(command.skillName))].push_back(&command);
		}

		for (const auto& entry : catalog.entries) {
			const std::wstring familyKey = entry.skillName;
			const auto normalizedFamilyKey = ToLower(Trim(familyKey));

			SkillsExecutionDependencyEntry dependencyRow;
			dependencyRow.familyKey = familyKey;
			if (entry.metadata.has_value()) {
				dependencyRow.requiredBins = NormalizeRequiredList(entry.metadata->requirements.bins);
				dependencyRow.requiredEnv = NormalizeRequiredList(entry.metadata->requirements.env);
				dependencyRow.requiredConfig = NormalizeRequiredList(entry.metadata->requirements.config);
			}

			const auto eligibilityIt = eligibilityBySkill.find(normalizedFamilyKey);
			if (eligibilityIt != eligibilityBySkill.end()) {
				const auto* eligible = eligibilityIt->second;
				dependencyRow.missingBins = NormalizeRequiredList(eligible->missingBins);
				dependencyRow.missingEnv = NormalizeRequiredList(eligible->missingEnv);
				dependencyRow.missingConfig = NormalizeRequiredList(eligible->missingConfig);
			}
			dependencyRow.ready =
				dependencyRow.missingBins.empty() &&
				dependencyRow.missingEnv.empty() &&
				dependencyRow.missingConfig.empty();
			report.dependencyEntries.push_back(dependencyRow);

			SkillsExecutionModeEntry modeRow;
			modeRow.familyKey = familyKey;
			if (!dependencyRow.ready) {
				modeRow.mode = SkillsExecutionReadinessMode::Blocked;
				modeRow.evidence = L"missing-dependencies";
			}
			else if (eligibilityIt != eligibilityBySkill.end() && !eligibilityIt->second->eligible) {
				modeRow.mode = appConfig.skills.remoteEligibility.enabled
					? SkillsExecutionReadinessMode::RemoteCapable
					: SkillsExecutionReadinessMode::Blocked;
				modeRow.evidence = appConfig.skills.remoteEligibility.enabled
					? L"remote-eligibility-enabled"
					: L"local-eligibility-failed";
			}
			else {
				modeRow.mode = SkillsExecutionReadinessMode::HostLocal;
				modeRow.evidence = L"local-dependencies-satisfied";
			}
			report.modeEntries.push_back(modeRow);

			SkillsExecutionToolMappingEntry mappingRow;
			mappingRow.familyKey = familyKey;
			if (entry.metadata.has_value() && !entry.metadata->skillKey.empty()) {
				mappingRow.expectedOpenClawTool = entry.metadata->skillKey;
			}
			else {
				mappingRow.expectedOpenClawTool = ToLower(Trim(familyKey));
			}

			const auto commandsIt = commandsBySkill.find(normalizedFamilyKey);
			if (commandsIt != commandsBySkill.end() && !commandsIt->second.empty()) {
				const auto* firstCommand = commandsIt->second.front();
				if (firstCommand->dispatch.enabled &&
					ToLower(Trim(firstCommand->dispatch.kind)) == L"tool" &&
					!Trim(firstCommand->dispatch.toolName).empty()) {
					mappingRow.mapped = true;
					mappingRow.blazeClawToolOrAdapter = firstCommand->dispatch.toolName;
					mappingRow.detail = L"mapped-via-command-dispatch";
				}
				else {
					mappingRow.mapped = false;
					mappingRow.blazeClawToolOrAdapter = firstCommand->name;
					mappingRow.detail = L"no-tool-dispatch-binding";
				}
			}
			else {
				mappingRow.mapped = false;
				mappingRow.detail = L"no-command-entry";
			}
			report.toolMappingEntries.push_back(mappingRow);

			SkillsExecutionSmokeEntry smokeRow;
			smokeRow.familyKey = familyKey;
			smokeRow.gatedByDependencies = true;
			if (!dependencyRow.ready) {
				smokeRow.status = SkillsExecutionReadinessSmokeStatus::Skipped;
				smokeRow.detail = L"skipped-missing-dependencies";
			}
			else if (modeRow.mode == SkillsExecutionReadinessMode::Blocked) {
				smokeRow.status = SkillsExecutionReadinessSmokeStatus::Skipped;
				smokeRow.detail = L"skipped-blocked";
			}
			else if (!mappingRow.mapped) {
				smokeRow.status = SkillsExecutionReadinessSmokeStatus::Skipped;
				smokeRow.detail = L"skipped-unmapped-tool";
			}
			else {
				smokeRow.status = SkillsExecutionReadinessSmokeStatus::Passed;
				smokeRow.detail = L"smoke-ready";
			}
			report.smokeEntries.push_back(smokeRow);

			SkillsExecutionCompatibilityMatrixRow matrixRow;
			matrixRow.familyKey = familyKey;
			matrixRow.mode = modeRow.mode;
			matrixRow.dependenciesReady = dependencyRow.ready;
			matrixRow.toolMappingReady = mappingRow.mapped;
			matrixRow.smokeStatus = smokeRow.status;
			matrixRow.notes = modeRow.evidence + L"; " + smokeRow.detail;
			report.compatibilityMatrix.push_back(matrixRow);
		}

		auto byFamily = [](const auto& left, const auto& right) {
			return ToLower(Trim(left.familyKey)) < ToLower(Trim(right.familyKey));
		};
		std::sort(report.dependencyEntries.begin(), report.dependencyEntries.end(), byFamily);
		std::sort(report.modeEntries.begin(), report.modeEntries.end(), byFamily);
		std::sort(report.toolMappingEntries.begin(), report.toolMappingEntries.end(), byFamily);
		std::sort(report.smokeEntries.begin(), report.smokeEntries.end(), byFamily);
		std::sort(report.compatibilityMatrix.begin(), report.compatibilityMatrix.end(), byFamily);

		return report;
	}

	std::string SkillsExecutionReadinessValidationService::BuildMarkdownReport(
		const SkillsExecutionReadinessValidationReport& report) const {
		std::ostringstream output;
		output << "# Skills Execution Readiness Validation Report (Phase 6)\n\n";

		auto writeHeader = [&output](const std::string& title) {
			output << "## " << title << "\n\n";
		};

		writeHeader("Dependency requirements");
		if (report.dependencyEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.dependencyEntries) {
				output << "- family=`" << WideToUtf8(row.familyKey) << "`"
					<< "; ready=" << (row.ready ? "true" : "false")
					<< "; missingBins=" << row.missingBins.size()
					<< "; missingEnv=" << row.missingEnv.size()
					<< "; missingConfig=" << row.missingConfig.size()
					<< "\n";
			}
			output << "\n";
		}

		writeHeader("Execution mode classification");
		if (report.modeEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.modeEntries) {
				output << "- family=`" << WideToUtf8(row.familyKey) << "`"
					<< "; mode=" << WideToUtf8(ReadinessModeLabel(row.mode))
					<< "; evidence=" << WideToUtf8(row.evidence)
					<< "\n";
			}
			output << "\n";
		}

		writeHeader("OpenClaw-to-BlazeClaw tool mapping");
		if (report.toolMappingEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.toolMappingEntries) {
				output << "- family=`" << WideToUtf8(row.familyKey) << "`"
					<< "; expected=`" << WideToUtf8(row.expectedOpenClawTool) << "`"
					<< "; mappedTo=`" << WideToUtf8(row.blazeClawToolOrAdapter) << "`"
					<< "; mapped=" << (row.mapped ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeHeader("Smoke-test gating and outcomes");
		if (report.smokeEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.smokeEntries) {
				output << "- family=`" << WideToUtf8(row.familyKey) << "`"
					<< "; status=" << WideToUtf8(SmokeStatusLabel(row.status))
					<< "; detail=" << WideToUtf8(row.detail)
					<< "\n";
			}
			output << "\n";
		}

		writeHeader("Compatibility matrix");
		if (report.compatibilityMatrix.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.compatibilityMatrix) {
				output << "- family=`" << WideToUtf8(row.familyKey) << "`"
					<< "; mode=" << WideToUtf8(ReadinessModeLabel(row.mode))
					<< "; dependenciesReady=" << (row.dependenciesReady ? "true" : "false")
					<< "; mappingReady=" << (row.toolMappingReady ? "true" : "false")
					<< "; smokeStatus=" << WideToUtf8(SmokeStatusLabel(row.smokeStatus))
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

	std::string SkillsExecutionReadinessValidationService::BuildJsonReport(
		const SkillsExecutionReadinessValidationReport& report) const {
		nlohmann::json root;
		root["workspaceRoot"] = report.workspaceRoot.generic_string();

		root["dependencyEntries"] = nlohmann::json::array();
		for (const auto& row : report.dependencyEntries) {
			nlohmann::json requiredBins = nlohmann::json::array();
			for (const auto& item : row.requiredBins) {
				requiredBins.push_back(WideToUtf8(item));
			}
			nlohmann::json requiredEnv = nlohmann::json::array();
			for (const auto& item : row.requiredEnv) {
				requiredEnv.push_back(WideToUtf8(item));
			}
			nlohmann::json requiredConfig = nlohmann::json::array();
			for (const auto& item : row.requiredConfig) {
				requiredConfig.push_back(WideToUtf8(item));
			}
			nlohmann::json missingBins = nlohmann::json::array();
			for (const auto& item : row.missingBins) {
				missingBins.push_back(WideToUtf8(item));
			}
			nlohmann::json missingEnv = nlohmann::json::array();
			for (const auto& item : row.missingEnv) {
				missingEnv.push_back(WideToUtf8(item));
			}
			nlohmann::json missingConfig = nlohmann::json::array();
			for (const auto& item : row.missingConfig) {
				missingConfig.push_back(WideToUtf8(item));
			}

			root["dependencyEntries"].push_back({
				{ "familyKey", WideToUtf8(row.familyKey) },
				{ "requiredBins", requiredBins },
				{ "requiredEnv", requiredEnv },
				{ "requiredConfig", requiredConfig },
				{ "missingBins", missingBins },
				{ "missingEnv", missingEnv },
				{ "missingConfig", missingConfig },
				{ "ready", row.ready },
			});
		}

		root["modeEntries"] = nlohmann::json::array();
		for (const auto& row : report.modeEntries) {
			root["modeEntries"].push_back({
				{ "familyKey", WideToUtf8(row.familyKey) },
				{ "mode", WideToUtf8(ReadinessModeLabel(row.mode)) },
				{ "evidence", WideToUtf8(row.evidence) },
			});
		}

		root["toolMappingEntries"] = nlohmann::json::array();
		for (const auto& row : report.toolMappingEntries) {
			root["toolMappingEntries"].push_back({
				{ "familyKey", WideToUtf8(row.familyKey) },
				{ "expectedOpenClawTool", WideToUtf8(row.expectedOpenClawTool) },
				{ "blazeClawToolOrAdapter", WideToUtf8(row.blazeClawToolOrAdapter) },
				{ "mapped", row.mapped },
				{ "detail", WideToUtf8(row.detail) },
			});
		}

		root["smokeEntries"] = nlohmann::json::array();
		for (const auto& row : report.smokeEntries) {
			root["smokeEntries"].push_back({
				{ "familyKey", WideToUtf8(row.familyKey) },
				{ "gatedByDependencies", row.gatedByDependencies },
				{ "status", WideToUtf8(SmokeStatusLabel(row.status)) },
				{ "detail", WideToUtf8(row.detail) },
			});
		}

		root["compatibilityMatrix"] = nlohmann::json::array();
		for (const auto& row : report.compatibilityMatrix) {
			root["compatibilityMatrix"].push_back({
				{ "familyKey", WideToUtf8(row.familyKey) },
				{ "mode", WideToUtf8(ReadinessModeLabel(row.mode)) },
				{ "dependenciesReady", row.dependenciesReady },
				{ "toolMappingReady", row.toolMappingReady },
				{ "smokeStatus", WideToUtf8(SmokeStatusLabel(row.smokeStatus)) },
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
