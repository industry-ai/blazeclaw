#include "pch.h"
#include "CServiceBootstrapCoordinator.h"

namespace blazeclaw::core {

	CServiceBootstrapCoordinator::RuntimeQueueSettings
		CServiceBootstrapCoordinator::ResolveRuntimeQueueSettings(
			const std::uint64_t defaultQueueWaitTimeoutMs,
			const std::uint64_t defaultExecutionTimeoutMs) const
	{
		return m_startupPolicyResolver.ResolveRuntimeQueueSettings(
			defaultQueueWaitTimeoutMs,
			defaultExecutionTimeoutMs);
	}

	CServiceBootstrapCoordinator::RuntimeOrchestrationPolicySettings
		CServiceBootstrapCoordinator::ResolveRuntimeOrchestrationPolicySettings() const {
		return m_startupPolicyResolver.ResolveRuntimeOrchestrationPolicySettings();
	}

	void CServiceBootstrapCoordinator::ValidateStartupFixtures(
		FixtureValidationContext& context) const
	{
		if (!context.enabled)
		{
			return;
		}

		std::wstring fixtureError;
		for (const auto& candidate : context.fixtureCandidates)
		{
			std::error_code ec;
			if (!std::filesystem::is_directory(candidate, ec) || ec)
			{
				continue;
			}

			const std::wstring candidateName = candidate.filename().wstring();
			if (candidateName == L"agents")
			{
				if (!context.agentsCatalogService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-scope fixture validation failed: " + fixtureError);
				}

				if (!context.agentsWorkspaceService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-workspace fixture validation failed: " + fixtureError);
				}

				if (!context.agentsToolPolicyService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-tool-policy fixture validation failed: " + fixtureError);
				}

				if (!context.agentsShellRuntimeService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-shell-runtime fixture validation failed: " + fixtureError);
				}

				if (!context.agentsModelRoutingService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-model-routing fixture validation failed: " + fixtureError);
				}

				if (!context.agentsAuthProfileService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-auth-profile fixture validation failed: " + fixtureError);
				}

				if (!context.agentsSandboxService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-sandbox fixture validation failed: " + fixtureError);
				}

				if (!context.agentsTranscriptSafetyService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-transcript fixture validation failed: " + fixtureError);
				}

				if (!context.subagentRegistryService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-subagent fixture validation failed: " + fixtureError);
				}

				if (!context.acpSpawnService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-acp fixture validation failed: " + fixtureError);
				}

				if (!context.embeddingsService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-embeddings fixture validation failed: " + fixtureError);
				}

				if (!context.retrievalMemoryService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-retrieval fixture validation failed: " + fixtureError);
				}

				if (!context.piEmbeddedService.ValidateFixtureScenarios(candidate, fixtureError))
				{
					context.warnings.push_back(
						L"agents-embedded fixture validation failed: " + fixtureError);
				}

				continue;
			}

			if (!context.skillsCatalogService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-catalog fixture validation failed: " + fixtureError);
			}

			if (!context.skillsEligibilityService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-eligibility fixture validation failed: " + fixtureError);
			}

			if (!context.skillsPromptService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-prompt fixture validation failed: " + fixtureError);
			}

			if (!context.skillsCommandService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-command fixture validation failed: " + fixtureError);
			}

			if (!context.skillsWatchService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-watch fixture validation failed: " + fixtureError);
			}

			if (!context.skillsSyncService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-sync fixture validation failed: " + fixtureError);
			}

			if (!context.skillsEnvOverrideService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-env fixture validation failed: " + fixtureError);
			}

			if (!context.skillsFacade.ValidateFixtureScenarios(
				candidate,
				fixtureError,
				context.skillsPromptService))
			{
				context.warnings.push_back(
					L"skills-facade fixture validation failed: " + fixtureError);
			}

			if (!context.skillsInstallService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-install fixture validation failed: " + fixtureError);
			}

			if (!context.skillSecurityScanService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"skills-scan fixture validation failed: " + fixtureError);
			}

			if (!context.hookCatalogService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"hooks-catalog fixture validation failed: " + fixtureError);
			}

			if (!context.hookEventService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"hooks-events fixture validation failed: " + fixtureError);
			}

			if (!context.hookExecutionService.ValidateFixtureScenarios(candidate, fixtureError))
			{
				context.warnings.push_back(
					L"hooks-execution fixture validation failed: " + fixtureError);
			}

			break;
		}
	}

	void CServiceBootstrapCoordinator::ValidateStartupFixtures(
		const FixtureValidationContext& context) const
	{
		auto copy = context;
		ValidateStartupFixtures(copy);
	}

	CServiceBootstrapCoordinator::HooksPolicySettings
		CServiceBootstrapCoordinator::ResolveHooksPolicySettings(
			const blazeclaw::config::AppConfig& config) const
	{
		return m_startupPolicyResolver.ResolveHooksPolicySettings(config);
	}

	CServiceBootstrapCoordinator::EmailPolicySettings
		CServiceBootstrapCoordinator::ResolveEmailPolicySettings(
			const blazeclaw::config::AppConfig& config) const
	{
		return m_startupPolicyResolver.ResolveEmailPolicySettings(config);
	}

	CServiceBootstrapCoordinator::ToolRuntimePolicySettings
		CServiceBootstrapCoordinator::ResolveToolRuntimePolicySettings() const
	{
		return m_startupPolicyResolver.ResolveToolRuntimePolicySettings();
	}

	void CServiceBootstrapCoordinator::AppendStartupTrace(const char* stage) const
	{
		m_startupPolicyResolver.AppendStartupTrace(stage);
	}

} // namespace blazeclaw::core
