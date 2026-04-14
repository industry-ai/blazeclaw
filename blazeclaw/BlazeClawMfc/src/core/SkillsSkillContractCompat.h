#pragma once

#include <filesystem>
#include <string>

namespace blazeclaw::core {

	enum class SkillSourceScope {
		User = 0,
		Project = 1,
		Temporary = 2,
	};

	enum class SkillSourceOrigin {
		Package = 0,
		TopLevel = 1,
	};

	struct SkillSourceInfoCompat {
		std::filesystem::path path;
		std::wstring source;
		SkillSourceScope scope = SkillSourceScope::Temporary;
		SkillSourceOrigin origin = SkillSourceOrigin::TopLevel;
		std::filesystem::path baseDir;
	};

	struct SkillPromptProjectionCompat {
		std::wstring name;
		std::wstring description;
		std::filesystem::path filePath;
		std::filesystem::path baseDir;
		std::wstring legacySource;
		SkillSourceInfoCompat sourceInfo;
	};

	[[nodiscard]] inline SkillSourceInfoCompat CreateSyntheticSkillSourceInfoCompat(
		const std::filesystem::path& path,
		const std::wstring& source,
		const SkillSourceScope scope = SkillSourceScope::Temporary,
		const SkillSourceOrigin origin = SkillSourceOrigin::TopLevel,
		const std::filesystem::path& baseDir = {}) {
		return SkillSourceInfoCompat{
			.path = path,
			.source = source,
			.scope = scope,
			.origin = origin,
			.baseDir = baseDir,
		};
	}

} // namespace blazeclaw::core
