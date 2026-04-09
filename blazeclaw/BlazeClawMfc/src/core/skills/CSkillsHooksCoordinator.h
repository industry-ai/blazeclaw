#pragma once

#include "../HookExecutionService.h"
#include "../HookCatalogService.h"
#include "../HookEventService.h"
#include "../SkillSecurityScanService.h"
#include "../SkillsCatalogService.h"
#include "../SkillsCommandService.h"
#include "../SkillsEligibilityService.h"
#include "../SkillsEnvOverrideService.h"
#include "../SkillsInstallService.h"
#include "../SkillsPromptService.h"
#include "../SkillsSyncService.h"
#include "../SkillsWatchService.h"
#include "../../config/ConfigModels.h"
#include "../../gateway/GatewayHost.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace blazeclaw::core {

	class CSkillsHooksCoordinator {
	public:
		struct RefreshContext {
			SkillsCatalogService& catalogService;
			SkillsEligibilityService& eligibilityService;
			HookCatalogService& hookCatalogService;
			HookExecutionService& hookExecutionService;
			SkillsPromptService& promptService;
			HookEventService& hookEventService;
			SkillsCommandService& commandService;
			SkillsSyncService& syncService;
			SkillsEnvOverrideService& envOverrideService;
			SkillsInstallService& installService;
			SkillSecurityScanService& securityScanService;
			SkillsWatchService& watchService;

			SkillsCatalogSnapshot& catalog;
			SkillsEligibilitySnapshot& eligibility;
			HookCatalogSnapshot& hookCatalog;
			HookExecutionSnapshot& hookExecution;
			SkillsPromptSnapshot& prompt;
			HookEventSnapshot& events;
			SkillsCommandSnapshot& commands;
			SkillsSyncSnapshot& sync;
			SkillsEnvOverrideSnapshot& envOverrides;
			SkillsInstallSnapshot& install;
			SkillSecurityScanSnapshot& securityScan;
			SkillsWatchSnapshot& watch;
			std::filesystem::path workspaceRoot;
			bool hooksFallbackPromptInjection = false;
		};

		struct GatewayStateContext {
			const SkillsCatalogSnapshot& catalog;
			const SkillsEligibilitySnapshot& eligibility;
			const SkillsPromptSnapshot& prompt;
			const SkillsCommandSnapshot& commands;
			const SkillsWatchSnapshot& watch;
			const SkillsSyncSnapshot& sync;
			const SkillsEnvOverrideSnapshot& envOverrides;
			const SkillsInstallSnapshot& install;
			const SkillSecurityScanSnapshot& securityScan;
			const HookExecutionSnapshot& hookExecution;

			bool hooksGovernanceReportingEnabled = false;
			std::wstring hooksLastGovernanceReportPath;
			std::uint64_t hooksGovernanceReportsGenerated = 0;
			bool hooksAutoRemediationEnabled = false;
			bool hooksAutoRemediationRequiresApproval = false;
			std::uint64_t hooksAutoRemediationExecuted = 0;
			std::wstring hooksLastAutoRemediationStatus;
			std::wstring hooksAutoRemediationTenantId;
			std::wstring hooksLastAutoRemediationPlaybookPath;
			std::uint32_t hooksAutoRemediationTokenMaxAgeMinutes = 0;
			std::uint64_t hooksAutoRemediationTokenRotations = 0;
			std::wstring hooksLastRemediationTelemetryPath;
			std::wstring hooksLastRemediationAuditPath;
			std::wstring hooksRemediationSloStatus;
			std::uint32_t hooksRemediationSloMaxDriftDetected = 0;
			std::uint32_t hooksRemediationSloMaxPolicyBlocked = 0;
			std::wstring hooksLastComplianceAttestationPath;
			std::wstring hooksEnterpriseSlaPolicyId;
			bool hooksCrossTenantAttestationAggregationEnabled = false;
			std::wstring hooksCrossTenantAttestationAggregationStatus;
			std::uint64_t hooksCrossTenantAttestationAggregationCount = 0;
			std::wstring hooksLastCrossTenantAttestationAggregationPath;
		};

		using EntryBuilder = std::function<blazeclaw::gateway::SkillsCatalogGatewayEntry(
			const SkillsCatalogEntry&,
			const SkillsEligibilityEntry*,
			const SkillsCommandSpec*,
			const SkillsInstallPlanEntry*)>;

		void RefreshSkillsState(
			const blazeclaw::config::AppConfig& config,
			bool forceRefresh,
			const std::wstring& reason,
			const RefreshContext& context) const;

		blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState(
			const GatewayStateContext& context,
			const EntryBuilder& entryBuilder,
			const std::function<std::string(const std::wstring&)>& toNarrow) const;
	};

} // namespace blazeclaw::core
