#pragma once

#include <string>

namespace blazeclaw::app::view_helpers {

	std::string ToNarrowUtf8(const std::wstring& value);
	std::string BuildSkillPathFromDeltaText(const std::string& text);
	std::vector<std::string> SplitTopLevelJsonObjects(const std::string& arrayJson);

} // namespace blazeclaw::app::view_helpers
