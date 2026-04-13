#pragma once

#include "../config/ConfigModels.h"

#include <map>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsConfigEvalPolicy {
		std::map<std::wstring, bool> configPathDefaults;
	};

	[[nodiscard]] const SkillsConfigEvalPolicy& DefaultSkillsConfigEvalPolicy();

	[[nodiscard]] std::wstring ResolveSkillsRuntimePlatform();

	[[nodiscard]] bool SkillsHasBinary(const std::wstring& binName);

	[[nodiscard]] std::vector<std::wstring> NormalizeSkillsAllowlistEntries(
		const std::vector<std::wstring>& rawEntries);

	[[nodiscard]] bool IsSkillsConfigPathTruthy(
		const blazeclaw::config::AppConfig& appConfig,
		const std::wstring& configPath,
		const SkillsConfigEvalPolicy& policy = DefaultSkillsConfigEvalPolicy());

} // namespace blazeclaw::core
