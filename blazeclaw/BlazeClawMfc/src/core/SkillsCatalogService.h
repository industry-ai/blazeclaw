#pragma once

#include "../config/ConfigModels.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	enum class SkillsSourceKind {
		Extra = 0,
		Plugin = 1,
		Bundled = 2,
		Managed = 3,
		Personal = 4,
		Project = 5,
		Workspace = 6,
		OpenClawOriginal = 7,
	};

	struct SkillsSourceRoot {
		SkillsSourceKind kind = SkillsSourceKind::Extra;
		int precedence = 0;
		std::filesystem::path configuredRoot;
		std::filesystem::path resolvedRoot;
	};

	struct SkillsLoaderPolicy {
		bool rejectPathSymlink = true;
		bool strictFrontmatter = false;
	};

	struct SkillFrontmatter {
		std::wstring name;
		std::wstring description;
		std::map<std::wstring, std::wstring> fields;
	};

	struct SkillsCatalogEntry {
		std::wstring skillName;
		std::wstring description;
		std::filesystem::path skillDir;
		std::filesystem::path skillFile;
		SkillsSourceKind sourceKind = SkillsSourceKind::Extra;
		int precedence = 0;
		bool validFrontmatter = false;
		std::vector<std::wstring> validationErrors;
		SkillFrontmatter frontmatter;
	};

	struct SkillsCatalogDiagnostics {
		std::vector<std::wstring> warnings;
		std::uint32_t rootsScanned = 0;
		std::uint32_t rootsSkipped = 0;
		std::uint32_t pluginRootsConfigured = 0;
		std::uint32_t pluginRootsScanned = 0;
		std::uint32_t loaderPolicyRejectPathSymlinkCount = 0;
		std::uint32_t loaderPolicyStrictFrontmatterCount = 0;
		std::uint32_t symlinkRejectedFiles = 0;
		std::uint32_t strictFrontmatterOmittedFiles = 0;
		std::uint32_t oversizedSkillFiles = 0;
		std::uint32_t invalidFrontmatterFiles = 0;
		std::uint32_t verifiedOpenPathFailures = 0;
		std::uint32_t verifiedOpenValidationFailures = 0;
		std::uint32_t verifiedOpenIoFailures = 0;
	};

	struct SkillsCatalogSnapshot {
		std::vector<SkillsCatalogEntry> entries;
		SkillsCatalogDiagnostics diagnostics;
	};

	class SkillsCatalogService {
	public:
		[[nodiscard]] SkillsCatalogSnapshot LoadCatalog(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;

		[[nodiscard]] static std::wstring SourceKindLabel(SkillsSourceKind kind);

	private:
		[[nodiscard]] static SkillsLoaderPolicy ResolveLoaderPolicy(
			const blazeclaw::config::AppConfig& appConfig);

		[[nodiscard]] std::vector<SkillsSourceRoot> BuildSourceRoots(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) const;

		[[nodiscard]] static std::optional<SkillFrontmatter> ParseFrontmatter(
			const std::wstring& skillContent,
			std::vector<std::wstring>& outValidationErrors);
	};

} // namespace blazeclaw::core
