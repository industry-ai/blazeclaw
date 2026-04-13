#pragma once

#include "../config/ConfigModels.h"

#include <map>
#include <string>

namespace blazeclaw::core {

	struct SkillsConfigEvalPolicy {
		std::map<std::wstring, bool> configPathDefaults;
	};

	[[nodiscard]] const SkillsConfigEvalPolicy& DefaultSkillsConfigEvalPolicy();

	[[nodiscard]] std::wstring ResolveSkillsRuntimePlatform();

	[[nodiscard]] bool SkillsHasBinary(const std::wstring& binName);

	[[nodiscard]] bool IsSkillsConfigPathTruthy(
		const blazeclaw::config::AppConfig& appConfig,
		const std::wstring& configPath,
		const SkillsConfigEvalPolicy& policy = DefaultSkillsConfigEvalPolicy());

} // namespace blazeclaw::core
