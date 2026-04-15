#pragma once

#include <string>
#include <unordered_set>

namespace blazeclaw::core {

	struct InlineActionsDecisionSignals {
		std::string slashCommandName;
		bool hasSlashCommand = false;
		bool hasExplicitSkillInvocation = false;
		bool shouldLoadSkillCommands = false;
	};

	class InlineActionsOrchestrationService {
	public:
		[[nodiscard]] std::string ResolveSlashCommandName(
			const std::string& commandBodyNormalized) const;

		[[nodiscard]] std::unordered_set<std::string>
			BuildBuiltinSlashCommands(
				const std::unordered_set<std::string>& reservedNames) const;

		[[nodiscard]] bool ShouldLoadSkillCommandsForSlash(
			bool allowTextCommands,
			const std::string& slashCommandName,
			const std::unordered_set<std::string>& builtinSlashCommands) const;

		[[nodiscard]] InlineActionsDecisionSignals BuildDecisionSignals(
			bool allowTextCommands,
			const std::string& commandBodyNormalized,
			const std::unordered_set<std::string>& reservedNames) const;
	};

} // namespace blazeclaw::core
