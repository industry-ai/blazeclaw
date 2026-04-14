#pragma once

#include "AgentsCatalogService.h"
#include "SkillsCatalogService.h"
#include "SkillsCommandService.h"
#include "SkillsEligibilityService.h"
#include "extensions/RuntimeCapabilityAdapterContracts.h"
#include "../config/ConfigModels.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct AgentSkillCommandDescriptor {
		std::wstring agentId;
		std::filesystem::path workspaceDir;
		std::optional<std::vector<std::wstring>> skillFilter;
	};

	struct AgentSkillCommandAggregationSnapshot {
		SkillsCommandSnapshot commandSnapshot;
		std::uint32_t descriptorsConsidered = 0;
		std::uint32_t workspaceGroupsConsidered = 0;
		std::uint32_t missingWorkspaceSkipped = 0;
		std::uint32_t unresolvedWorkspaceSkipped = 0;
	};

	struct AgentSkillCommandAggregationContext {
		const std::vector<AgentSkillCommandDescriptor>& descriptors;
		const blazeclaw::config::AppConfig& appConfig;
		SkillsCatalogService& catalogService;
		SkillsEligibilityService& eligibilityService;
		SkillsCommandService& commandService;
		const std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>*
			commandSourceAdapters = nullptr;
		std::vector<std::wstring> reservedNames;
	};

	class SkillCommandsAggregationService {
	public:
		[[nodiscard]] AgentSkillCommandAggregationSnapshot BuildSnapshot(
			const AgentSkillCommandAggregationContext& context) const;
	};

} // namespace blazeclaw::core
