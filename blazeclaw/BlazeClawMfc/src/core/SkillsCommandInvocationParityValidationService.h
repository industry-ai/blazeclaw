#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsCommandInvocationSanitizationParityEntry {
		std::wstring skillName;
		std::wstring expectedSanitizedCommand;
		std::wstring actualCommand;
		bool matches = false;
	};

	struct SkillsCommandInvocationDedupeParityEntry {
		std::wstring baseCommand;
		std::wstring winnerCommand;
		std::vector<std::wstring> collidedCommands;
		bool reservedCollision = false;
		bool deterministic = true;
		std::vector<std::wstring> issues;
	};

	struct SkillsCommandInvocationSkillResolutionParityEntry {
		std::wstring invocation;
		std::wstring expectedSkillName;
		std::wstring resolvedSkillName;
		std::wstring resolvedCommandName;
		std::wstring resolvedArgs;
		bool matches = false;
	};

	struct SkillsCommandInvocationDirectResolutionParityEntry {
		std::wstring invocation;
		std::wstring expectedCommandName;
		std::wstring resolvedCommandName;
		std::wstring resolvedSkillName;
		std::wstring resolvedArgs;
		bool matches = false;
	};

	struct SkillsCommandInvocationDispatchParityEntry {
		std::wstring commandName;
		bool dispatchExpected = false;
		bool dispatchEnabled = false;
		std::wstring expectedKind;
		std::wstring actualKind;
		std::wstring expectedToolName;
		std::wstring actualToolName;
		bool matches = false;
	};

	struct SkillsCommandInvocationRewriteParityEntry {
		std::wstring invocation;
		std::wstring commandName;
		bool rewriteExpected = false;
		bool rewriteProduced = false;
		std::string rewrittenPromptUtf8;
		bool matches = false;
		std::vector<std::wstring> issues;
	};

	struct SkillsCommandInvocationParityValidationReport {
		std::filesystem::path workspaceRoot;
		std::vector<SkillsCommandInvocationSanitizationParityEntry> sanitizationParity;
		std::vector<SkillsCommandInvocationDedupeParityEntry> dedupeParity;
		std::vector<SkillsCommandInvocationSkillResolutionParityEntry> skillResolutionParity;
		std::vector<SkillsCommandInvocationDirectResolutionParityEntry> directResolutionParity;
		std::vector<SkillsCommandInvocationDispatchParityEntry> dispatchParity;
		std::vector<SkillsCommandInvocationRewriteParityEntry> rewriteParity;
		std::vector<std::wstring> warnings;
	};

	class SkillsCommandInvocationParityValidationService {
	public:
		[[nodiscard]] SkillsCommandInvocationParityValidationReport BuildReport(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] std::string BuildMarkdownReport(
			const SkillsCommandInvocationParityValidationReport& report) const;

		[[nodiscard]] std::string BuildJsonReport(
			const SkillsCommandInvocationParityValidationReport& report) const;
	};

} // namespace blazeclaw::core
