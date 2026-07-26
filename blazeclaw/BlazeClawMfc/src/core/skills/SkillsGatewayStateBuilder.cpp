// SkillsGatewayStateBuilder.cpp
#include "pch.h"
#include "SkillsGatewayStateBuilder.h"

#include <algorithm>
#include <unordered_map>

namespace blazeclaw::core {

	blazeclaw::gateway::SkillsCatalogGatewayState
		SkillsGatewayStateBuilder::Build(
			const CSkillsHooksCoordinator::GatewayStateContext& context,
			const EntryBuilder& entryBuilder,
			const std::function<std::string(const std::wstring&)>& toNarrow) const
	{
		const auto& skills = context.skills;
		const auto& hooks = context.hooks;
		const auto& governance = context.governance;
		const auto& remediation = context.remediation;
		const auto& compliance = context.compliance;

		blazeclaw::gateway::SkillsCatalogGatewayState state;
		state.entries.reserve(skills.catalog.entries.size());

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& eligibility : skills.eligibility.entries)
		{
			eligibilityByName.emplace(eligibility.skillName, eligibility);
		}

		std::unordered_map<std::wstring, SkillsCommandSpec> commandsByName;
		for (const auto& command : skills.commands.commands)
		{
			commandsByName.emplace(command.skillName, command);
		}

		std::unordered_map<std::wstring, SkillsInstallPlanEntry> installByName;
		for (const auto& install : skills.install.entries)
		{
			installByName.emplace(install.skillName, install);
		}

		for (const auto& entry : skills.catalog.entries)
		{
			const auto eligibilityIt = eligibilityByName.find(entry.skillName);
			const auto commandIt = commandsByName.find(entry.skillName);
			const auto installIt = installByName.find(entry.skillName);

			state.entries.push_back(entryBuilder(
				entry,
				eligibilityIt == eligibilityByName.end() ? nullptr : &eligibilityIt->second,
				commandIt == commandsByName.end() ? nullptr : &commandIt->second,
				installIt == installByName.end() ? nullptr : &installIt->second));
		}

		state.rootsScanned = skills.catalog.diagnostics.rootsScanned;
		state.rootsSkipped = skills.catalog.diagnostics.rootsSkipped;
		state.pluginRootsConfigured = skills.catalog.diagnostics.pluginRootsConfigured;
		state.pluginRootsScanned = skills.catalog.diagnostics.pluginRootsScanned;

		state.warningCount = skills.catalog.diagnostics.warnings.size();

		state.eligibleCount = skills.eligibility.eligibleCount;
		state.disabledCount = skills.eligibility.disabledCount;
		state.blockedByAllowlistCount = skills.eligibility.blockedByAllowlistCount;
		state.missingRequirementsCount = skills.eligibility.missingRequirementsCount;

		state.envBlocked = skills.envOverrides.blockedCount;

		state.installExecutableCount = skills.install.executableCount;
		state.installBlockedCount = skills.install.blockedCount;

		state.scanInfoCount = skills.securityScan.infoCount;
		state.scanWarnCount = skills.securityScan.warnCount;
		state.scanCriticalCount = skills.securityScan.criticalCount;
		state.scanScannedFiles = skills.securityScan.scannedFileCount;

		state.governanceReportingEnabled = governance.reportingEnabled;
		state.governanceReportsGenerated =
			static_cast<std::size_t>(governance.reportsGenerated);
		state.lastGovernanceReportPath =
			toNarrow(governance.lastReportPath);

		state.policyBlockedCount =
			static_cast<std::size_t>(hooks.hookExecution.diagnostics.policyBlockedCount);
		state.driftDetectedCount =
			static_cast<std::size_t>(hooks.hookExecution.diagnostics.driftDetectedCount);
		state.lastDriftReason =
			toNarrow(hooks.hookExecution.diagnostics.lastDriftReason);

		state.autoRemediationEnabled = remediation.enabled;
		state.autoRemediationRequiresApproval = remediation.requiresApproval;
		state.autoRemediationExecuted =
			static_cast<std::size_t>(remediation.executed);
		state.lastAutoRemediationStatus =
			toNarrow(remediation.lastStatus);
		state.autoRemediationTenantId =
			toNarrow(remediation.tenantId);
		state.lastAutoRemediationPlaybookPath =
			toNarrow(remediation.lastPlaybookPath);
		state.autoRemediationTokenMaxAgeMinutes =
			static_cast<std::size_t>(remediation.tokenMaxAgeMinutes);
		state.autoRemediationTokenRotations =
			static_cast<std::size_t>(remediation.tokenRotations);
		state.lastRemediationTelemetryPath =
			toNarrow(remediation.lastTelemetryPath);
		state.lastRemediationAuditPath =
			toNarrow(remediation.lastAuditPath);
		state.remediationSloStatus =
			toNarrow(remediation.sloStatus);
		state.remediationSloMaxDriftDetected =
			static_cast<std::size_t>(remediation.sloMaxDriftDetected);
		state.remediationSloMaxPolicyBlocked =
			static_cast<std::size_t>(remediation.sloMaxPolicyBlocked);

		state.lastComplianceAttestationPath =
			toNarrow(compliance.lastComplianceAttestationPath);
		state.enterpriseSlaPolicyId =
			toNarrow(compliance.enterpriseSlaPolicyId);
		state.crossTenantAttestationAggregationEnabled =
			compliance.crossTenantAttestationAggregationEnabled;
		state.crossTenantAttestationAggregationStatus =
			toNarrow(compliance.crossTenantAttestationAggregationStatus);
		state.crossTenantAttestationAggregationCount =
			static_cast<std::size_t>(compliance.crossTenantAttestationAggregationCount);
		state.lastCrossTenantAttestationAggregationPath =
			toNarrow(compliance.lastCrossTenantAttestationAggregationPath);

		return state;
	}

}