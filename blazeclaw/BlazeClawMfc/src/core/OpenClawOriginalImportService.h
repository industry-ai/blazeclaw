#pragma once

#include "../config/ConfigModels.h"
#include "SkillsContracts.h"
#include "SkillsFrontmatterCompat.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	enum class OpenClawOriginalImportActivationState {
		Detected = 0,
		Imported = 1,
		ToolEnabled = 2,
		Failed = 3,
	};

	struct OpenClawOriginalImportRequest {
		std::filesystem::path workspaceRoot;
		std::filesystem::path skillDir;
		std::filesystem::path skillFile;
		bool validFrontmatter = false;
		std::optional<ParsedSkillFrontmatterCompat> frontmatter;
	};

	struct OpenClawOriginalImportResult {
		OpenClawOriginalImportActivationState activationState =
			OpenClawOriginalImportActivationState::Detected;
		std::wstring origin = L"openclaw-original";
		std::vector<std::wstring> diagnostics;
		std::optional<SkillsMetadataSpec> normalizedMetadata;
		bool metadataConvertedFromClawdbot = false;
		bool hasToolManifest = false;
		std::filesystem::path promotedDir;
	};

	class OpenClawOriginalImportService {
	public:
		[[nodiscard]] OpenClawOriginalImportResult ImportSkill(
			const OpenClawOriginalImportRequest& request,
			const blazeclaw::config::AppConfig& appConfig) const;

	private:
		[[nodiscard]] static std::filesystem::path ResolveManagedPromotionDir(
			const std::filesystem::path& workspaceRoot,
			const std::wstring& skillName);
	};

} // namespace blazeclaw::core
