#pragma once

#include "config/ConfigModels.h"

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core::servicemanager_skill_roots {

	std::string NormalizeDirectoryPathUtf8(const std::filesystem::path& path);
	std::vector<std::string> BuildCanonicalSkillRootSnapshot(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& config);

} // namespace blazeclaw::core::servicemanager_skill_roots
