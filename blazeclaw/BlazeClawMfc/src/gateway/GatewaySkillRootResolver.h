#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace blazeclaw::gateway::skills {

enum class SkillRootKind {
	Bundled,
	Core,
	OpenClawOriginal,
};

struct SkillRootResolution {
	SkillRootKind kind = SkillRootKind::Core;
	std::vector<std::filesystem::path> resolvedRoots;
	std::vector<std::filesystem::path> inspectedRoots;
};

inline const char* KindName(const SkillRootKind kind) {
	switch (kind) {
	case SkillRootKind::Bundled:
		return "skills-bundled";
	case SkillRootKind::Core:
		return "skills";
	case SkillRootKind::OpenClawOriginal:
		return "skills-openclaw-original";
	default:
		return "skills";
	}
}

inline void PushUniqueNormalizedPath(
	std::vector<std::filesystem::path>& ordered,
	std::set<std::filesystem::path>& seen,
	const std::filesystem::path& value) {
	if (value.empty()) {
		return;
	}

	std::error_code ec;
	const auto normalized = std::filesystem::weakly_canonical(value, ec);
	const auto candidate = ec ? value.lexically_normal() : normalized;
	if (candidate.empty()) {
		return;
	}

	if (seen.insert(candidate).second) {
		ordered.push_back(candidate);
	}
}

inline std::vector<std::filesystem::path> BuildOrderedRoots(
	const std::filesystem::path& moduleDir,
	const std::filesystem::path& currentDir) {
	std::vector<std::filesystem::path> ordered;
	std::set<std::filesystem::path> seen;

	PushUniqueNormalizedPath(ordered, seen, moduleDir);
	if (!moduleDir.empty() && moduleDir.has_parent_path()) {
		PushUniqueNormalizedPath(ordered, seen, moduleDir.parent_path());
	}

	std::filesystem::path cursor = moduleDir;
	for (int i = 0; i < 4 && !cursor.empty(); ++i) {
		PushUniqueNormalizedPath(ordered, seen, cursor);
		if (!cursor.has_parent_path()) {
			break;
		}
		auto parent = cursor.parent_path();
		if (parent == cursor) {
			break;
		}
		cursor = parent;
	}

	PushUniqueNormalizedPath(ordered, seen, currentDir);
	return ordered;
}

inline std::optional<std::filesystem::path> ReadOverrideRoot(
	const std::string& value,
	const std::string& expectedLeaf) {
	if (value.empty()) {
		return std::nullopt;
	}

	const std::filesystem::path explicitRoot(value);
	if (explicitRoot.empty()) {
		return std::nullopt;
	}

	std::error_code ec;
	const auto normalized = std::filesystem::weakly_canonical(explicitRoot, ec);
	const auto root = ec ? explicitRoot.lexically_normal() : normalized;
	if (root.empty()) {
		return std::nullopt;
	}

	if (!expectedLeaf.empty() && root.filename().string() == expectedLeaf) {
		return root;
	}

	const auto nested = root / expectedLeaf;
	if (std::filesystem::exists(nested)) {
		return nested;
	}

	return root;
}

inline SkillRootResolution ResolveSkillRoots(
	const SkillRootKind kind,
	const std::filesystem::path& moduleDir,
	const std::filesystem::path& currentDir,
	const std::optional<std::string>& genericOverride,
	const std::optional<std::string>& kindOverride) {
	SkillRootResolution resolution;
	resolution.kind = kind;

	std::vector<std::filesystem::path> ordered;
	std::set<std::filesystem::path> seen;

	const std::string leaf = KindName(kind);
	if (kindOverride.has_value()) {
		if (const auto overrideRoot = ReadOverrideRoot(kindOverride.value(), leaf); overrideRoot.has_value()) {
			PushUniqueNormalizedPath(ordered, seen, overrideRoot.value());
		}
	}
	if (genericOverride.has_value()) {
		if (const auto overrideRoot = ReadOverrideRoot(genericOverride.value(), leaf); overrideRoot.has_value()) {
			PushUniqueNormalizedPath(ordered, seen, overrideRoot.value());
		}
	}

	for (const auto& root : BuildOrderedRoots(moduleDir, currentDir)) {
		PushUniqueNormalizedPath(ordered, seen, root / "blazeclaw" / leaf);
		PushUniqueNormalizedPath(ordered, seen, root / leaf);
	}

	resolution.inspectedRoots = ordered;
	for (const auto& candidate : ordered) {
		std::error_code ec;
		if (std::filesystem::exists(candidate, ec) && std::filesystem::is_directory(candidate, ec)) {
			resolution.resolvedRoots.push_back(candidate);
		}
	}

	return resolution;
}

} // namespace blazeclaw::gateway::skills
