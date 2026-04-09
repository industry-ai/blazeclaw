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
		using ToolRuntimePolicySettings =
			bootstrap::StartupPolicyResolver::ToolRuntimePolicySettings;
		using EmailPolicySettings =
			bootstrap::StartupPolicyResolver::EmailPolicySettings;
		using HooksPolicySettings =
			bootstrap::StartupPolicyResolver::HooksPolicySettings;
		struct RuntimeQueueSettings {
			bool asyncQueueEnabled = true;
			std::uint64_t queueWaitTimeoutMs = 15000;
			std::uint64_t executionTimeoutMs = 120000;
		};

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
			SkillsInstallService& skillsInstallService;
			SkillSecurityScanService& skillSecurityScanService;
			HookCatalogService& hookCatalogService;
			HookEventService& hookEventService;
			HookExecutionService& hookExecutionService;
		};

		RuntimeQueueSettings ResolveRuntimeQueueSettings(
			const std::function<bool(const wchar_t* key, bool fallback)>& readBool,
			const std::function<std::uint64_t(
				const wchar_t* key,
				std::uint64_t fallback)>& readUInt64,
			std::uint64_t defaultQueueWaitTimeoutMs,
			std::uint64_t defaultExecutionTimeoutMs) const;

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
