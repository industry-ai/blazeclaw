#include "pch.h"
#include "SkillsCommandInvocationParityValidationService.h"

#include "SkillCommandInvocationService.h"
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

		constexpr std::size_t kMaxCommandLength = 32;

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
			for (const wchar_t ch : value) {
				output.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
			}
			return output;
		}

		std::wstring SanitizeCommandName(const std::wstring& raw) {
			std::wstring normalized;
			normalized.reserve(raw.size());

			for (const wchar_t ch : ToLower(raw)) {
				const bool isAlphaNum =
					(ch >= L'a' && ch <= L'z') ||
					(ch >= L'0' && ch <= L'9') ||
					ch == L'_';
				if (isAlphaNum) {
					normalized.push_back(ch);
					continue;
				}

				if (!normalized.empty() && normalized.back() != L'_') {
					normalized.push_back(L'_');
				}
			}

			while (!normalized.empty() && normalized.front() == L'_') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == L'_') {
				normalized.pop_back();
			}

			if (normalized.empty()) {
				normalized = L"skill";
			}
			if (normalized.size() > kMaxCommandLength) {
				normalized.resize(kMaxCommandLength);
			}

			return normalized;
		}

		std::wstring ReplaceArgsPlaceholder(
			const std::wstring& promptTemplate,
			const std::wstring& args) {
			std::wstring rewritten = promptTemplate;
			const std::wstring placeholder = L"{{args}}";
			const auto position = rewritten.find(placeholder);
			if (position != std::wstring::npos) {
				rewritten.replace(position, placeholder.size(), args);
			}
			return rewritten;
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& pathValue) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(pathValue, ec);
			if (ec) {
				return pathValue.lexically_normal();
			}
			return canonical;
		}

	} // namespace

	SkillsCommandInvocationParityValidationReport SkillsCommandInvocationParityValidationService::BuildReport(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsCommandInvocationParityValidationReport report;
		report.workspaceRoot = CanonicalOrSelf(workspaceRoot);

		SkillsCatalogService catalogService;
		const auto catalog = catalogService.LoadCatalog(workspaceRoot, appConfig);
		SkillsEligibilityService eligibilityService;
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		SkillsCommandService commandService;
		const auto commandSnapshot = commandService.BuildSnapshot(catalog, eligibility);
		const auto commandSnapshotWithReserved = commandService.BuildSnapshot(
			catalog,
			eligibility,
			{ L"skill", L"help" });
		SkillCommandInvocationService invocationService;

		std::unordered_map<std::wstring, const SkillsCatalogEntry*> catalogBySkill;
		for (const auto& entry : catalog.entries) {
			catalogBySkill.insert_or_assign(ToLower(Trim(entry.skillName)), &entry);
		}

		for (const auto& command : commandSnapshot.commands) {
			const std::wstring expectedBase = SanitizeCommandName(command.skillName);
			SkillsCommandInvocationSanitizationParityEntry row;
			row.skillName = command.skillName;
			row.expectedSanitizedCommand = expectedBase;
			row.actualCommand = command.name;
			row.matches = ToLower(Trim(command.name)) == ToLower(Trim(expectedBase)) ||
				(ToLower(Trim(command.name)).rfind(ToLower(Trim(expectedBase + L"_")), 0) == 0);
			report.sanitizationParity.push_back(std::move(row));

			SkillsCommandInvocationSkillResolutionParityEntry skillResolution;
			std::wstring skillLookup = command.skillName;
			if (skillLookup.find_first_of(L" \t\r\n") != std::wstring::npos) {
				skillLookup = command.name;
				report.warnings.push_back(
					L"Skill name contains whitespace; using command alias for /skill parity check: " +
					command.skillName);
			}
			skillResolution.invocation = L"/skill " + skillLookup + L" hello";
			skillResolution.expectedSkillName = command.skillName;
			if (const auto resolved = invocationService.ResolveInvocation(
				skillResolution.invocation,
				commandSnapshot.commands);
				resolved.has_value()) {
				skillResolution.resolvedSkillName = resolved->command.skillName;
				skillResolution.resolvedCommandName = resolved->command.name;
				skillResolution.resolvedArgs = resolved->args.value_or(L"");
				skillResolution.matches =
					ToLower(Trim(skillResolution.resolvedSkillName)) ==
					ToLower(Trim(skillResolution.expectedSkillName));
			}
			report.skillResolutionParity.push_back(std::move(skillResolution));

			SkillsCommandInvocationDirectResolutionParityEntry directResolution;
			directResolution.invocation = L"/" + command.name + L" hello";
			directResolution.expectedCommandName = command.name;
			if (const auto resolved = invocationService.ResolveInvocation(
				directResolution.invocation,
				commandSnapshot.commands);
				resolved.has_value()) {
				directResolution.resolvedCommandName = resolved->command.name;
				directResolution.resolvedSkillName = resolved->command.skillName;
				directResolution.resolvedArgs = resolved->args.value_or(L"");
				directResolution.matches =
					ToLower(Trim(directResolution.expectedCommandName)) ==
					ToLower(Trim(directResolution.resolvedCommandName));
			}
			report.directResolutionParity.push_back(std::move(directResolution));

			SkillsCommandInvocationDispatchParityEntry dispatchParity;
			dispatchParity.commandName = command.name;
			dispatchParity.dispatchEnabled = command.dispatch.enabled;
			dispatchParity.actualKind = command.dispatch.kind;
			dispatchParity.actualToolName = command.dispatch.toolName;

			const auto catalogIt = catalogBySkill.find(ToLower(Trim(command.skillName)));
			if (catalogIt == catalogBySkill.end()) {
				dispatchParity.matches = false;
			}
			else {
				const auto* entry = catalogIt->second;
				const auto expectedDispatch = ToLower(Trim(
					entry->frontmatter.fields.contains(L"command-dispatch")
					? entry->frontmatter.fields.at(L"command-dispatch")
					: (entry->frontmatter.fields.contains(L"command_dispatch")
						? entry->frontmatter.fields.at(L"command_dispatch")
						: L"")));
				dispatchParity.dispatchExpected = expectedDispatch == L"tool";
				dispatchParity.expectedKind = dispatchParity.dispatchExpected ? L"tool" : L"";
				dispatchParity.expectedToolName = Trim(
					entry->frontmatter.fields.contains(L"command-tool")
					? entry->frontmatter.fields.at(L"command-tool")
					: (entry->frontmatter.fields.contains(L"command_tool")
						? entry->frontmatter.fields.at(L"command_tool")
						: L""));
				dispatchParity.matches = dispatchParity.dispatchExpected == dispatchParity.dispatchEnabled;
				if (dispatchParity.dispatchExpected) {
					dispatchParity.matches = dispatchParity.matches &&
						ToLower(Trim(dispatchParity.actualKind)) == L"tool" &&
						ToLower(Trim(dispatchParity.actualToolName)) ==
						ToLower(Trim(dispatchParity.expectedToolName));
				}
			}
			report.dispatchParity.push_back(std::move(dispatchParity));

			SkillsCommandInvocationRewriteParityEntry rewriteParity;
			rewriteParity.commandName = command.name;
			rewriteParity.invocation = L"/" + command.name + L" hello";
			rewriteParity.rewriteExpected =
				!(command.dispatch.enabled && ToLower(Trim(command.dispatch.kind)) == L"tool");
			const auto rewritten = invocationService.RewriteInvocationPromptUtf8(
				WideToUtf8(rewriteParity.invocation),
				commandSnapshot.commands);
			rewriteParity.rewriteProduced = rewritten.has_value();
			rewriteParity.rewrittenPromptUtf8 = rewritten.value_or(std::string());
			rewriteParity.matches = rewriteParity.rewriteExpected == rewriteParity.rewriteProduced;
			if (rewritten.has_value() && !command.promptTemplate.empty()) {
				const auto expected = WideToUtf8(
					Trim(ReplaceArgsPlaceholder(command.promptTemplate, L"hello")));
				if (rewritten.value() != expected) {
					rewriteParity.matches = false;
					rewriteParity.issues.push_back(L"prompt-template-rewrite-mismatch");
				}
			}
			report.rewriteParity.push_back(std::move(rewriteParity));
		}

		std::map<std::wstring, std::vector<std::wstring>> commandsByBase;
		const std::set<std::wstring> reserved{ L"skill", L"help" };
		for (const auto& command : commandSnapshotWithReserved.commands) {
			commandsByBase[SanitizeCommandName(command.skillName)].push_back(command.name);
		}
		for (auto& [base, names] : commandsByBase) {
			std::sort(names.begin(), names.end());
			const bool hasCollision = names.size() > 1 || reserved.contains(base);
			if (!hasCollision) {
				continue;
			}

			SkillsCommandInvocationDedupeParityEntry dedupe;
			dedupe.baseCommand = base;
			dedupe.collidedCommands = names;
			dedupe.winnerCommand = names.empty() ? L"" : names.front();
			dedupe.reservedCollision = reserved.contains(base);
			dedupe.deterministic = !names.empty();
			if (names.empty()) {
				dedupe.issues.push_back(L"no-dedupe-candidate");
			}
			report.dedupeParity.push_back(std::move(dedupe));
		}

		auto normalizeSort = [](const auto& entry) {
			if constexpr (requires { entry.skillName; }) {
				return ToLower(Trim(entry.skillName));
			}
			if constexpr (requires { entry.commandName; }) {
				return ToLower(Trim(entry.commandName));
			}
			if constexpr (requires { entry.baseCommand; }) {
				return ToLower(Trim(entry.baseCommand));
			}
			return std::wstring();
		};

		std::sort(
			report.sanitizationParity.begin(),
			report.sanitizationParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationSanitizationParityEntry& left,
				const SkillsCommandInvocationSanitizationParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});
		std::sort(
			report.dedupeParity.begin(),
			report.dedupeParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationDedupeParityEntry& left,
				const SkillsCommandInvocationDedupeParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});
		std::sort(
			report.skillResolutionParity.begin(),
			report.skillResolutionParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationSkillResolutionParityEntry& left,
				const SkillsCommandInvocationSkillResolutionParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});
		std::sort(
			report.directResolutionParity.begin(),
			report.directResolutionParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationDirectResolutionParityEntry& left,
				const SkillsCommandInvocationDirectResolutionParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});
		std::sort(
			report.dispatchParity.begin(),
			report.dispatchParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationDispatchParityEntry& left,
				const SkillsCommandInvocationDispatchParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});
		std::sort(
			report.rewriteParity.begin(),
			report.rewriteParity.end(),
			[&normalizeSort](
				const SkillsCommandInvocationRewriteParityEntry& left,
				const SkillsCommandInvocationRewriteParityEntry& right) {
				return normalizeSort(left) < normalizeSort(right);
			});

		return report;
	}

	std::string SkillsCommandInvocationParityValidationService::BuildMarkdownReport(
		const SkillsCommandInvocationParityValidationReport& report) const {
		std::ostringstream output;
		output << "# Skills Command Invocation Parity Validation Report (Phase 5)\n\n";

		auto writeSectionHeader = [&output](const std::string& title) {
			output << "## " << title << "\n\n";
		};

		writeSectionHeader("Sanitized command name parity");
		if (report.sanitizationParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.sanitizationParity) {
				output << "- skill=`" << WideToUtf8(row.skillName) << "`"
					<< "; expected=`" << WideToUtf8(row.expectedSanitizedCommand) << "`"
					<< "; actual=`" << WideToUtf8(row.actualCommand) << "`"
					<< "; match=" << (row.matches ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeSectionHeader("Reserved-name dedupe parity");
		if (report.dedupeParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.dedupeParity) {
				output << "- base=`" << WideToUtf8(row.baseCommand) << "`"
					<< "; winner=`" << WideToUtf8(row.winnerCommand) << "`"
					<< "; reservedCollision=" << (row.reservedCollision ? "true" : "false")
					<< "; deterministic=" << (row.deterministic ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeSectionHeader("`/skill <name>` resolution parity");
		if (report.skillResolutionParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.skillResolutionParity) {
				output << "- invocation=`" << WideToUtf8(row.invocation) << "`"
					<< "; expectedSkill=`" << WideToUtf8(row.expectedSkillName) << "`"
					<< "; resolvedSkill=`" << WideToUtf8(row.resolvedSkillName) << "`"
					<< "; match=" << (row.matches ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeSectionHeader("Direct `/<command>` resolution parity");
		if (report.directResolutionParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.directResolutionParity) {
				output << "- invocation=`" << WideToUtf8(row.invocation) << "`"
					<< "; expectedCommand=`" << WideToUtf8(row.expectedCommandName) << "`"
					<< "; resolvedCommand=`" << WideToUtf8(row.resolvedCommandName) << "`"
					<< "; match=" << (row.matches ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeSectionHeader("`command-dispatch` parity");
		if (report.dispatchParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.dispatchParity) {
				output << "- command=`" << WideToUtf8(row.commandName) << "`"
					<< "; expectedDispatch=" << (row.dispatchExpected ? "true" : "false")
					<< "; actualDispatch=" << (row.dispatchEnabled ? "true" : "false")
					<< "; match=" << (row.matches ? "true" : "false")
					<< "\n";
			}
			output << "\n";
		}

		writeSectionHeader("Prompt-template rewrite parity");
		if (report.rewriteParity.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& row : report.rewriteParity) {
				output << "- command=`" << WideToUtf8(row.commandName) << "`"
					<< "; rewriteExpected=" << (row.rewriteExpected ? "true" : "false")
					<< "; rewriteProduced=" << (row.rewriteProduced ? "true" : "false")
					<< "; match=" << (row.matches ? "true" : "false")
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

	std::string SkillsCommandInvocationParityValidationService::BuildJsonReport(
		const SkillsCommandInvocationParityValidationReport& report) const {
		nlohmann::json root;
		root["workspaceRoot"] = report.workspaceRoot.generic_string();

		root["sanitizationParity"] = nlohmann::json::array();
		for (const auto& row : report.sanitizationParity) {
			root["sanitizationParity"].push_back({
				{ "skillName", WideToUtf8(row.skillName) },
				{ "expectedSanitizedCommand", WideToUtf8(row.expectedSanitizedCommand) },
				{ "actualCommand", WideToUtf8(row.actualCommand) },
				{ "matches", row.matches },
			});
		}

		root["dedupeParity"] = nlohmann::json::array();
		for (const auto& row : report.dedupeParity) {
			nlohmann::json collided = nlohmann::json::array();
			for (const auto& item : row.collidedCommands) {
				collided.push_back(WideToUtf8(item));
			}
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : row.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["dedupeParity"].push_back({
				{ "baseCommand", WideToUtf8(row.baseCommand) },
				{ "winnerCommand", WideToUtf8(row.winnerCommand) },
				{ "collidedCommands", collided },
				{ "reservedCollision", row.reservedCollision },
				{ "deterministic", row.deterministic },
				{ "issues", issues },
			});
		}

		root["skillResolutionParity"] = nlohmann::json::array();
		for (const auto& row : report.skillResolutionParity) {
			root["skillResolutionParity"].push_back({
				{ "invocation", WideToUtf8(row.invocation) },
				{ "expectedSkillName", WideToUtf8(row.expectedSkillName) },
				{ "resolvedSkillName", WideToUtf8(row.resolvedSkillName) },
				{ "resolvedCommandName", WideToUtf8(row.resolvedCommandName) },
				{ "resolvedArgs", WideToUtf8(row.resolvedArgs) },
				{ "matches", row.matches },
			});
		}

		root["directResolutionParity"] = nlohmann::json::array();
		for (const auto& row : report.directResolutionParity) {
			root["directResolutionParity"].push_back({
				{ "invocation", WideToUtf8(row.invocation) },
				{ "expectedCommandName", WideToUtf8(row.expectedCommandName) },
				{ "resolvedCommandName", WideToUtf8(row.resolvedCommandName) },
				{ "resolvedSkillName", WideToUtf8(row.resolvedSkillName) },
				{ "resolvedArgs", WideToUtf8(row.resolvedArgs) },
				{ "matches", row.matches },
			});
		}

		root["dispatchParity"] = nlohmann::json::array();
		for (const auto& row : report.dispatchParity) {
			root["dispatchParity"].push_back({
				{ "commandName", WideToUtf8(row.commandName) },
				{ "dispatchExpected", row.dispatchExpected },
				{ "dispatchEnabled", row.dispatchEnabled },
				{ "expectedKind", WideToUtf8(row.expectedKind) },
				{ "actualKind", WideToUtf8(row.actualKind) },
				{ "expectedToolName", WideToUtf8(row.expectedToolName) },
				{ "actualToolName", WideToUtf8(row.actualToolName) },
				{ "matches", row.matches },
			});
		}

		root["rewriteParity"] = nlohmann::json::array();
		for (const auto& row : report.rewriteParity) {
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : row.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["rewriteParity"].push_back({
				{ "invocation", WideToUtf8(row.invocation) },
				{ "commandName", WideToUtf8(row.commandName) },
				{ "rewriteExpected", row.rewriteExpected },
				{ "rewriteProduced", row.rewriteProduced },
				{ "rewrittenPromptUtf8", row.rewrittenPromptUtf8 },
				{ "matches", row.matches },
				{ "issues", issues },
			});
		}

		root["warnings"] = nlohmann::json::array();
		for (const auto& warning : report.warnings) {
			root["warnings"].push_back(WideToUtf8(warning));
		}

		return root.dump(2);
	}

} // namespace blazeclaw::core
