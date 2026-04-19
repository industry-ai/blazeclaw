#pragma once

#include "../config/ConfigModels.h"
#include "AgentsCatalogService.h"
#include "SkillCommandsAggregationService.h"

#include <vector>

namespace blazeclaw::core {

/// Resolves per-agent skill command descriptors and reserved slash names from config and scope
/// (policy-only; no refresh orchestration).
struct SkillsAgentCommandDescriptorPolicy {
	[[nodiscard]] static std::vector<AgentSkillCommandDescriptor> BuildDescriptors(
		const blazeclaw::config::AppConfig& config,
		const AgentScopeSnapshot& agentsScope);

	[[nodiscard]] static std::vector<std::wstring> BuildReservedChatSlashCommandNamesNormalized();
};

} // namespace blazeclaw::core
