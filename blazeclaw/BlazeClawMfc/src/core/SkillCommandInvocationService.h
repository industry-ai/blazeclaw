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
	};

} // namespace blazeclaw::core
