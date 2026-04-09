#include "pch.h"
#include "CSkillsHooksCoordinator.h"

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

		context.catalog = context.catalogService.LoadCatalog(workspaceRoot, config);
		context.eligibility = context.eligibilityService.Evaluate(context.catalog, config);
		context.hookCatalog = context.hookCatalogService.BuildSnapshot(context.catalog);
		context.hookExecution = context.hookExecutionService.Snapshot();
		context.prompt = context.promptService.BuildSnapshot(
			context.catalog,
			context.eligibility,
			config,
			std::nullopt,
			context.hooksFallbackPromptInjection);
		context.events = context.hookEventService.Snapshot();
		context.commands = context.commandService.BuildSnapshot(
			context.catalog,
			context.eligibility);
		context.sync = context.syncService.SyncToSandbox(
			workspaceRoot,
			context.catalog,
			context.eligibility,
			config);
		context.envOverrides = context.envOverrideService.BuildSnapshot(
			context.catalog,
			context.eligibility,
			config);
		context.install = context.installService.BuildSnapshot(
			context.catalog,
			context.eligibility,
			config);
		context.securityScan = context.securityScanService.BuildSnapshot(
			context.catalog,
			context.eligibility,
			config);
		context.envOverrideService.Apply(context.envOverrides);
		context.watch = context.watchService.Observe(
			context.catalog,
			config,
			forceRefresh,
			reason);
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
		gatewaySkillsState.oversizedSkillFiles =
			context.catalog.diagnostics.oversizedSkillFiles;
		gatewaySkillsState.invalidFrontmatterFiles =
			context.catalog.diagnostics.invalidFrontmatterFiles;
		gatewaySkillsState.warningCount = context.catalog.diagnostics.warnings.size();
		gatewaySkillsState.eligibleCount = context.eligibility.eligibleCount;
		gatewaySkillsState.disabledCount = context.eligibility.disabledCount;
		gatewaySkillsState.blockedByAllowlistCount =
			context.eligibility.blockedByAllowlistCount;
		gatewaySkillsState.missingRequirementsCount =
			context.eligibility.missingRequirementsCount;
		gatewaySkillsState.promptIncludedCount = context.prompt.includedCount;
		gatewaySkillsState.promptChars = context.prompt.promptChars;
		gatewaySkillsState.promptTruncated = context.prompt.truncated;
		gatewaySkillsState.snapshotVersion = context.watch.version;
		gatewaySkillsState.watchEnabled = context.watch.watchEnabled;
		gatewaySkillsState.watchDebounceMs = context.watch.debounceMs;
		gatewaySkillsState.watchReason = toNarrow(context.watch.reason);
		gatewaySkillsState.prompt = toNarrow(context.prompt.prompt);
		gatewaySkillsState.sandboxSyncOk = context.sync.success;
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

} // namespace blazeclaw::core
