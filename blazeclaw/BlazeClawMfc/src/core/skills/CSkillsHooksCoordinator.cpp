#include "pch.h"
#include "CSkillsHooksCoordinator.h"

#include <algorithm>
#include <unordered_map>

namespace blazeclaw::core {

	void CSkillsHooksCoordinator::RefreshSkillsState(
		const blazeclaw::config::AppConfig& config,
		const bool forceRefresh,
		const std::wstring& reason,
		const RefreshContext& context) const
	{
		auto workspaceRoot = context.workspaceRoot;
		if (workspaceRoot.empty())
		{
			workspaceRoot = std::filesystem::current_path();
		}

		auto refresh = context.skillsFacade.RefreshSkillsState(
			workspaceRoot,
			config,
			forceRefresh,
			reason,
			context.hooksFallbackPromptInjection,
			context.refreshDependencies);

		context.catalog = std::move(refresh.catalog);
		context.eligibility = std::move(refresh.eligibility);
		context.hookCatalog = context.hookCatalogService.BuildSnapshot(context.catalog);
		context.hookExecution = context.hookExecutionService.Snapshot();
		context.prompt = std::move(refresh.prompt);
		context.events = context.hookEventService.Snapshot();
		context.commands = std::move(refresh.commands);
		context.sync = std::move(refresh.sync);
		context.envOverrides = std::move(refresh.envOverrides);
		context.install = std::move(refresh.install);
		context.securityScan = std::move(refresh.securityScan);
		context.watch = std::move(refresh.watch);
		context.runSnapshot = std::move(refresh.runSnapshot);
	}

	blazeclaw::gateway::SkillsCatalogGatewayState
		CSkillsHooksCoordinator::BuildGatewaySkillsState(
			const GatewayStateContext& context,
			const EntryBuilder& entryBuilder,
			const std::function<std::string(const std::wstring&)>& toNarrow) const
	{
		blazeclaw::gateway::SkillsCatalogGatewayState gatewaySkillsState;
		gatewaySkillsState.entries.reserve(context.catalog.entries.size());

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& eligibility : context.eligibility.entries)
		{
			eligibilityByName.emplace(eligibility.skillName, eligibility);
		}

		std::unordered_map<std::wstring, SkillsCommandSpec> commandsBySkill;
		for (const auto& command : context.commands.commands)
		{
			commandsBySkill.emplace(command.skillName, command);
		}

		std::unordered_map<std::wstring, SkillsInstallPlanEntry> installBySkill;
		for (const auto& plan : context.install.entries)
		{
			installBySkill.emplace(plan.skillName, plan);
		}

		for (const auto& entry : context.catalog.entries)
		{
			const auto eligibilityIt = eligibilityByName.find(entry.skillName);
			const auto commandIt = commandsBySkill.find(entry.skillName);
			const auto installIt = installBySkill.find(entry.skillName);

			gatewaySkillsState.entries.push_back(entryBuilder(
				entry,
				eligibilityIt != eligibilityByName.end() ? &eligibilityIt->second : nullptr,
				commandIt != commandsBySkill.end() ? &commandIt->second : nullptr,
				installIt != installBySkill.end() ? &installIt->second : nullptr));
		}

		gatewaySkillsState.rootsScanned = context.catalog.diagnostics.rootsScanned;
		gatewaySkillsState.rootsSkipped = context.catalog.diagnostics.rootsSkipped;
		gatewaySkillsState.pluginRootsConfigured =
			context.catalog.diagnostics.pluginRootsConfigured;
		gatewaySkillsState.pluginRootsScanned =
			context.catalog.diagnostics.pluginRootsScanned;
		gatewaySkillsState.loaderPolicyRejectPathSymlinkCount =
			context.catalog.diagnostics.loaderPolicyRejectPathSymlinkCount;
		gatewaySkillsState.loaderPolicyStrictFrontmatterCount =
			context.catalog.diagnostics.loaderPolicyStrictFrontmatterCount;
		gatewaySkillsState.symlinkRejectedFiles =
			context.catalog.diagnostics.symlinkRejectedFiles;
		gatewaySkillsState.strictFrontmatterOmittedFiles =
			context.catalog.diagnostics.strictFrontmatterOmittedFiles;
		gatewaySkillsState.oversizedSkillFiles =
			context.catalog.diagnostics.oversizedSkillFiles;
		gatewaySkillsState.invalidFrontmatterFiles =
			context.catalog.diagnostics.invalidFrontmatterFiles;
		gatewaySkillsState.verifiedOpenPathFailures =
			context.catalog.diagnostics.verifiedOpenPathFailures;
		gatewaySkillsState.verifiedOpenValidationFailures =
			context.catalog.diagnostics.verifiedOpenValidationFailures;
		gatewaySkillsState.verifiedOpenIoFailures =
			context.catalog.diagnostics.verifiedOpenIoFailures;
		gatewaySkillsState.warningCount = context.catalog.diagnostics.warnings.size();
		gatewaySkillsState.eligibleCount = context.eligibility.eligibleCount;
		gatewaySkillsState.disabledCount = context.eligibility.disabledCount;
		gatewaySkillsState.blockedByAllowlistCount =
			context.eligibility.blockedByAllowlistCount;
		gatewaySkillsState.missingRequirementsCount =
			context.eligibility.missingRequirementsCount;
		gatewaySkillsState.strictEntryResolutionModeCount =
			context.eligibility.strictEntryResolutionModeCount;
		gatewaySkillsState.compatEntryResolutionModeCount =
			context.eligibility.compatEntryResolutionModeCount;
		gatewaySkillsState.configResolvedByKeyCount =
			context.eligibility.configResolvedByKeyCount;
		gatewaySkillsState.configResolvedByNameFallbackCount =
			context.eligibility.configResolvedByNameFallbackCount;
		gatewaySkillsState.allowlistRawCount =
			context.eligibility.allowlistRawCount;
		gatewaySkillsState.allowlistNormalizedCount =
			context.eligibility.allowlistNormalizedCount;
		gatewaySkillsState.remoteEligibilityEnabledCount =
			context.eligibility.remoteEligibilityEnabledCount;
		gatewaySkillsState.remotePlatformSatisfiedCount =
			context.eligibility.remotePlatformSatisfiedCount;
		gatewaySkillsState.remoteBinSatisfiedCount =
			context.eligibility.remoteBinSatisfiedCount;
		gatewaySkillsState.remoteAnyBinSatisfiedCount =
			context.eligibility.remoteAnyBinSatisfiedCount;
		gatewaySkillsState.alwaysBypassCount =
			context.eligibility.alwaysBypassCount;
		gatewaySkillsState.commandSanitizeCount =
			context.commands.sanitizeCount;
		gatewaySkillsState.commandDedupeCount =
			context.commands.dedupeCount;
		gatewaySkillsState.commandMissingToolDispatchCount =
			context.commands.missingToolDispatchCount;
		gatewaySkillsState.commandInvalidArgModeFallbackCount =
			context.commands.invalidArgModeFallbackCount;
		gatewaySkillsState.commandSourceContributionCount =
			context.commands.commandSourceContributionCount;
		gatewaySkillsState.promptIncludedCount = context.prompt.includedCount;
		gatewaySkillsState.promptChars = context.prompt.promptChars;
		gatewaySkillsState.promptTruncated = context.prompt.truncated;
		gatewaySkillsState.snapshotVersion = context.watch.version;
		gatewaySkillsState.watchEnabled = context.watch.watchEnabled;
		gatewaySkillsState.watchDebounceMs = context.watch.debounceMs;
		gatewaySkillsState.watchReason = toNarrow(context.watch.reason);
		gatewaySkillsState.prompt = toNarrow(context.prompt.prompt);
		gatewaySkillsState.sandboxSyncOk = context.sync.success;
		gatewaySkillsState.sandboxDestinationNamingMode =
			toNarrow(context.sync.destinationNamingMode);
		gatewaySkillsState.sandboxDestinationCollisions =
			context.sync.destinationNameCollisions;
		gatewaySkillsState.sandboxSourceDirFallbacks =
			context.sync.sourceDirFallbackCount;
		gatewaySkillsState.sandboxSynced = context.sync.copiedSkills;
		gatewaySkillsState.sandboxSkipped = context.sync.skippedSkills;
		gatewaySkillsState.envAllowed = context.envOverrides.allowedCount;
		gatewaySkillsState.envBlocked = context.envOverrides.blockedCount;
		gatewaySkillsState.installExecutableCount = context.install.executableCount;
		gatewaySkillsState.installBlockedCount = context.install.blockedCount;
		gatewaySkillsState.scanInfoCount = context.securityScan.infoCount;
		gatewaySkillsState.scanWarnCount = context.securityScan.warnCount;
		gatewaySkillsState.scanCriticalCount = context.securityScan.criticalCount;
		gatewaySkillsState.scanScannedFiles = context.securityScan.scannedFileCount;
		gatewaySkillsState.governanceReportingEnabled =
			context.hooksGovernanceReportingEnabled;
		gatewaySkillsState.governanceReportsGenerated =
			static_cast<std::size_t>(context.hooksGovernanceReportsGenerated);
		gatewaySkillsState.lastGovernanceReportPath =
			toNarrow(context.hooksLastGovernanceReportPath);
		gatewaySkillsState.policyBlockedCount =
			static_cast<std::size_t>(context.hookExecution.diagnostics.policyBlockedCount);
		gatewaySkillsState.driftDetectedCount =
			static_cast<std::size_t>(context.hookExecution.diagnostics.driftDetectedCount);
		gatewaySkillsState.lastDriftReason =
			toNarrow(context.hookExecution.diagnostics.lastDriftReason);
		gatewaySkillsState.autoRemediationEnabled = context.hooksAutoRemediationEnabled;
		gatewaySkillsState.autoRemediationRequiresApproval =
			context.hooksAutoRemediationRequiresApproval;
		gatewaySkillsState.autoRemediationExecuted =
			static_cast<std::size_t>(context.hooksAutoRemediationExecuted);
		gatewaySkillsState.lastAutoRemediationStatus =
			toNarrow(context.hooksLastAutoRemediationStatus);
		gatewaySkillsState.autoRemediationTenantId =
			toNarrow(context.hooksAutoRemediationTenantId);
		gatewaySkillsState.lastAutoRemediationPlaybookPath =
			toNarrow(context.hooksLastAutoRemediationPlaybookPath);
		gatewaySkillsState.autoRemediationTokenMaxAgeMinutes =
			static_cast<std::size_t>(context.hooksAutoRemediationTokenMaxAgeMinutes);
		gatewaySkillsState.autoRemediationTokenRotations =
			static_cast<std::size_t>(context.hooksAutoRemediationTokenRotations);
		gatewaySkillsState.lastRemediationTelemetryPath =
			toNarrow(context.hooksLastRemediationTelemetryPath);
		gatewaySkillsState.lastRemediationAuditPath =
			toNarrow(context.hooksLastRemediationAuditPath);
		gatewaySkillsState.remediationSloStatus =
			toNarrow(context.hooksRemediationSloStatus);
		gatewaySkillsState.remediationSloMaxDriftDetected =
			static_cast<std::size_t>(context.hooksRemediationSloMaxDriftDetected);
		gatewaySkillsState.remediationSloMaxPolicyBlocked =
			static_cast<std::size_t>(context.hooksRemediationSloMaxPolicyBlocked);
		gatewaySkillsState.lastComplianceAttestationPath =
			toNarrow(context.hooksLastComplianceAttestationPath);
		gatewaySkillsState.enterpriseSlaPolicyId =
			toNarrow(context.hooksEnterpriseSlaPolicyId);
		gatewaySkillsState.crossTenantAttestationAggregationEnabled =
			context.hooksCrossTenantAttestationAggregationEnabled;
		gatewaySkillsState.crossTenantAttestationAggregationStatus =
			toNarrow(context.hooksCrossTenantAttestationAggregationStatus);
		gatewaySkillsState.crossTenantAttestationAggregationCount =
			static_cast<std::size_t>(context.hooksCrossTenantAttestationAggregationCount);
		gatewaySkillsState.lastCrossTenantAttestationAggregationPath =
			toNarrow(context.hooksLastCrossTenantAttestationAggregationPath);

		return gatewaySkillsState;
	}

	void CSkillsHooksCoordinator::EmitGovernanceAndRemediation(
		const HooksGovernanceEmitter::GovernanceContext& governanceContext,
		const HooksGovernanceEmitter::RemediationContext& remediationContext,
		std::vector<std::wstring>& inOutWarnings) const
	{
		HooksGovernanceEmitter emitter;
		emitter.EmitGovernanceReportIfNeeded(governanceContext, inOutWarnings);
		emitter.EmitRemediationLifecycleIfNeeded(remediationContext, inOutWarnings);
	}

	void CSkillsHooksCoordinator::ApplyHookBootstrapProjection(
		HookBootstrapProjectionContext& context) const
	{
		context.selfEvolvingHookTriggered =
			ContainsBootstrapFile(context.bootstrapFiles, L"SELF_EVOLVING_REMINDER.md");

		if (context.selfEvolvingHookTriggered &&
			context.prompt.find(L"## Self-Evolving Reminder") == std::wstring::npos)
		{
			context.prompt +=
				L"\n## Self-Evolving Reminder\n"
				L"When tasks finish, capture reusable learnings:\n"
				L"- corrections -> .learnings/LEARNINGS.md\n"
				L"- failures -> .learnings/ERRORS.md\n"
				L"- missing capabilities -> .learnings/FEATURE_REQUESTS.md\n"
				L"Promote proven patterns to AGENTS.md / SOUL.md / TOOLS.md.\n";
			context.promptChars = static_cast<std::uint32_t>(context.prompt.size());
			if (context.prompt.size() > context.maxSkillsPromptChars)
			{
				context.prompt = context.prompt.substr(0, context.maxSkillsPromptChars);
				context.promptChars =
					static_cast<std::uint32_t>(context.prompt.size());
				context.promptTruncated = true;
			}

			context.lastReminderState = L"reminder_fallback_used";
			context.lastReminderReason = L"prompt_fallback";
		}

		std::wstringstream builder;
		bool headerWritten = false;
		for (const auto& file : context.bootstrapFiles)
		{
			auto normalized = file.path;
			std::transform(
				normalized.begin(),
				normalized.end(),
				normalized.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			if (normalized == L"self_evolving_reminder.md")
			{
				continue;
			}

			if (!headerWritten)
			{
				builder << L"\n## Hook Bootstrap Context\n";
				headerWritten = true;
			}

			builder << L"- " << file.path;
			if (file.virtualFile)
			{
				builder << L" (virtual)";
			}
			builder << L"\n";
		}

		const std::wstring genericHookContext = builder.str();
		if (!genericHookContext.empty() &&
			context.prompt.find(L"## Hook Bootstrap Context") == std::wstring::npos)
		{
			context.prompt += genericHookContext;
			context.promptChars = static_cast<std::uint32_t>(context.prompt.size());
			if (context.prompt.size() > context.maxSkillsPromptChars)
			{
				context.prompt = context.prompt.substr(0, context.maxSkillsPromptChars);
				context.promptChars =
					static_cast<std::uint32_t>(context.prompt.size());
				context.promptTruncated = true;
			}
		}
	}

	bool CSkillsHooksCoordinator::ContainsBootstrapFile(
		const std::vector<HookBootstrapFile>& files,
		const std::wstring& expectedPath)
	{
		for (const auto& file : files)
		{
			auto lowered = file.path;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			auto expected = expectedPath;
			std::transform(
				expected.begin(),
				expected.end(),
				expected.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			if (lowered == expected)
			{
				return true;
			}
		}

		return false;
	}

} // namespace blazeclaw::core
