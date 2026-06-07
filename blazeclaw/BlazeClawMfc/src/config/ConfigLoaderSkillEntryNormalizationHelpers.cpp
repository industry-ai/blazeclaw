#include "pch.h"
#include "ConfigLoaderSkillEntryNormalizationHelpers.h"

#include <cwctype>

namespace blazeclaw::config::skill_entry_normalization {

	std::wstring NormalizeSkillEntryConfigKey(const std::wstring& raw) {
		std::wstring normalized;
		normalized.reserve(raw.size());
		for (const wchar_t ch : raw) {
			if (std::iswspace(ch) != 0) {
				continue;
			}

			const wchar_t lowered = static_cast<wchar_t>(std::towlower(ch));
			if ((lowered >= L'a' && lowered <= L'z') ||
				(lowered >= L'0' && lowered <= L'9') ||
				lowered == L'.' ||
				lowered == L'_' ||
				lowered == L'-') {
				normalized.push_back(lowered);
			}
		}

		return normalized;
	}

} // namespace blazeclaw::config::skill_entry_normalization
