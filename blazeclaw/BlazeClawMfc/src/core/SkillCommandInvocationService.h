#pragma once

#include "SkillsCommandService.h"

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillCommandInvocationResult {
		SkillsCommandSpec command;
		std::optional<std::wstring> args;
	};

	class SkillCommandInvocationService {
	public:
		[[nodiscard]] std::optional<SkillCommandInvocationResult> ResolveInvocation(
			const std::wstring& commandBodyNormalized,
			const std::vector<SkillsCommandSpec>& skillCommands) const;

		/// Rewrites a slash/skill invocation into a prompt for the agent (template + {{args}}).
		[[nodiscard]] std::optional<std::string> RewriteInvocationPromptUtf8(
			const std::string& commandBodyNormalizedUtf8,
			const std::vector<SkillsCommandSpec>& skillCommands) const;
	};

} // namespace blazeclaw::core
