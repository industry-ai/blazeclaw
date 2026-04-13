#pragma once

#include "StartupPolicyResolver.h"
#include "StartupFixtureValidator.h"

#include "../AcpSpawnService.h"
#include "../AgentsAuthProfileService.h"
#include "../AgentsCatalogService.h"
#include "../AgentsModelRoutingService.h"
#include "../AgentsSandboxService.h"
#include "../AgentsShellRuntimeService.h"
#include "../AgentsToolPolicyService.h"
#include "../AgentsTranscriptSafetyService.h"
#include "../AgentsWorkspaceService.h"
#include "../HookCatalogService.h"
#include "../HookEventService.h"
#include "../HookExecutionService.h"
#include "../OnnxEmbeddingsService.h"
#include "../PiEmbeddedService.h"
#include "../RetrievalMemoryService.h"
#include "../SkillSecurityScanService.h"
#include "../SkillsCatalogService.h"
#include "../SkillsCommandService.h"
#include "../SkillsEligibilityService.h"
#include "../SkillsEnvOverrideService.h"
#include "../SkillsFacade.h"
#include "../SkillsInstallService.h"
#include "../SkillsPromptService.h"
#include "../SkillsSyncService.h"
#include "../SkillsWatchService.h"
#include "../SubagentRegistryService.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	class CServiceBootstrapCoordinator {
	public:
		using RuntimeQueueSettings =
			bootstrap::StartupPolicyResolver::RuntimeQueueSettings;
		using RuntimeOrchestrationPolicySettings =
			bootstrap::StartupPolicyResolver::RuntimeOrchestrationPolicySettings;
		using ToolRuntimePolicySettings =
			bootstrap::StartupPolicyResolver::ToolRuntimePolicySettings;
		using EmailPolicySettings =
			bootstrap::StartupPolicyResolver::EmailPolicySettings;
		using HooksPolicySettings =
			bootstrap::StartupPolicyResolver::HooksPolicySettings;
		struct FixtureValidationContext {
			bool enabled = false;
			std::vector<std::filesystem::path> fixtureCandidates;
			std::vector<std::wstring>& warnings;

			AgentsCatalogService& agentsCatalogService;
			AgentsWorkspaceService& agentsWorkspaceService;
			AgentsToolPolicyService& agentsToolPolicyService;
			AgentsShellRuntimeService& agentsShellRuntimeService;
			AgentsModelRoutingService& agentsModelRoutingService;
			AgentsAuthProfileService& agentsAuthProfileService;
			AgentsSandboxService& agentsSandboxService;
			AgentsTranscriptSafetyService& agentsTranscriptSafetyService;
			SubagentRegistryService& subagentRegistryService;
			AcpSpawnService& acpSpawnService;
			OnnxEmbeddingsService& embeddingsService;
			RetrievalMemoryService& retrievalMemoryService;
			PiEmbeddedService& piEmbeddedService;

			SkillsCatalogService& skillsCatalogService;
			SkillsEligibilityService& skillsEligibilityService;
			SkillsPromptService& skillsPromptService;
			SkillsCommandService& skillsCommandService;
			SkillsWatchService& skillsWatchService;
			SkillsSyncService& skillsSyncService;
			SkillsEnvOverrideService& skillsEnvOverrideService;
			SkillsFacade& skillsFacade;
			SkillsInstallService& skillsInstallService;
			SkillSecurityScanService& skillSecurityScanService;
			HookCatalogService& hookCatalogService;
			HookEventService& hookEventService;
			HookExecutionService& hookExecutionService;
		};

		RuntimeQueueSettings ResolveRuntimeQueueSettings(
			std::uint64_t defaultQueueWaitTimeoutMs,
			std::uint64_t defaultExecutionTimeoutMs) const;

		[[nodiscard]] RuntimeOrchestrationPolicySettings
			ResolveRuntimeOrchestrationPolicySettings() const;

		void ValidateStartupFixtures(FixtureValidationContext& context) const;
		void ValidateStartupFixtures(const FixtureValidationContext& context) const;

		[[nodiscard]] HooksPolicySettings ResolveHooksPolicySettings(
			const blazeclaw::config::AppConfig& config) const;

		[[nodiscard]] EmailPolicySettings ResolveEmailPolicySettings(
			const blazeclaw::config::AppConfig& config) const;

		[[nodiscard]] ToolRuntimePolicySettings ResolveToolRuntimePolicySettings() const;

		void AppendStartupTrace(const char* stage) const;

		[[nodiscard]] static constexpr const wchar_t*
			StartupFixtureValidationEnvKey()
		{
			return bootstrap::StartupFixtureValidator::kEnvStartupValidationEnabled;
		}

	private:
		bootstrap::StartupPolicyResolver m_startupPolicyResolver;
	};

} // namespace blazeclaw::core
