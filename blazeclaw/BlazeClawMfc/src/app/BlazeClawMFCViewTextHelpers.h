#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::app::view_helpers {

	std::string ToNarrowUtf8(const std::wstring& value);
	std::string BuildSkillPathFromDeltaText(const std::string& text);
	std::vector<std::string> SplitTopLevelJsonObjects(const std::string& arrayJson);
	std::string NormalizeSkillKeyForPath(const std::string& skillKey);
	std::optional<std::filesystem::path> FindEmailConfigHtml(
		const std::filesystem::path& start);
	std::optional<std::filesystem::path> FindSkillConfigHtml(
		const std::filesystem::path& start,
		const std::string& skillKey);

} // namespace blazeclaw::app::view_helpers
