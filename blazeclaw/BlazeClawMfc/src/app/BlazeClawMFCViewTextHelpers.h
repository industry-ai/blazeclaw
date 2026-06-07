#pragma once

#include <string>

namespace blazeclaw::app::view_helpers {

	std::string ToNarrowUtf8(const std::wstring& value);
	std::string BuildSkillPathFromDeltaText(const std::string& text);

} // namespace blazeclaw::app::view_helpers
