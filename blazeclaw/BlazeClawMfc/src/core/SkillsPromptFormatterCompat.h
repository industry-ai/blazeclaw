#pragma once

#include "SkillsSkillContractCompat.h"

#include <string>
#include <vector>

namespace blazeclaw::core {

	[[nodiscard]] std::wstring EscapeSkillPromptXmlCompat(
		const std::wstring& value);

	[[nodiscard]] std::wstring FormatSkillsForPromptCompat(
		const std::vector<SkillPromptProjectionCompat>& skills);

	[[nodiscard]] std::wstring FormatSkillsForPromptCompactCompat(
		const std::vector<SkillPromptProjectionCompat>& skills);

} // namespace blazeclaw::core
