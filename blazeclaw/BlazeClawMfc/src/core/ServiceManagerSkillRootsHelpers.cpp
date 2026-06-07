#include "pch.h"
#include "ServiceManagerSkillRootsHelpers.h"

#include <set>

namespace blazeclaw::core::servicemanager_skill_roots {

	std::string NormalizeDirectoryPathUtf8(const std::filesystem::path& path) {
		if (path.empty()) {
			return {};
		}

		std::error_code ec;
		const auto canonical = std::filesystem::weakly_canonical(path, ec);
		const auto normalized = ec ? path.lexically_normal() : canonical.lexically_normal();
		return normalized.string();
	}

	std::vector<std::string> BuildCanonicalSkillRootSnapshot(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& config) {
		std::vector<std::string> roots;
		std::set<std::string> seen;
		const auto pushUnique = [&roots, &seen](const std::filesystem::path& path) {
			const std::string normalized = NormalizeDirectoryPathUtf8(path);
			if (normalized.empty()) {
				return;
			}
			if (seen.insert(normalized).second) {
				roots.push_back(normalized);
			}
		};

		const auto pushIfDir = [&pushUnique](const std::filesystem::path& path) {
			std::error_code ec;
			if (std::filesystem::is_directory(path, ec) && !ec) {
				pushUnique(path);
			}
		};

		pushIfDir(workspaceRoot / L"skills-bundled");
		pushIfDir(workspaceRoot / L"skills");
		pushIfDir(workspaceRoot / L"skills-openclaw-original");
		pushIfDir(workspaceRoot / L"blazeclaw" / L"skills-bundled");
		pushIfDir(workspaceRoot / L"blazeclaw" / L"skills");
		pushIfDir(workspaceRoot / L"blazeclaw" / L"skills-openclaw-original");

		const std::filesystem::path managedOpenClawRoot =
			workspaceRoot / L".blazeclaw" / L"skills" / L"openclaw-original";
		pushIfDir(managedOpenClawRoot);

		if (!config.skills.openclawOriginal.sourceDir.empty()) {
			pushIfDir(workspaceRoot / std::filesystem::path(config.skills.openclawOriginal.sourceDir));
		}

		return roots;
	}

} // namespace blazeclaw::core::servicemanager_skill_roots
