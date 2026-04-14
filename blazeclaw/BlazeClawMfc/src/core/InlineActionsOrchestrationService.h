#pragma once

#include <string>
#include <unordered_set>

namespace blazeclaw::core {

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
	};

} // namespace blazeclaw::core
