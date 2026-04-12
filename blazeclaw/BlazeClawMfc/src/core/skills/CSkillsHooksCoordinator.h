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
#include "../SkillsFacade.h"
#include "../SkillsSyncService.h"
#include "../SkillsWatchService.h"
#include "HooksGovernanceEmitter.h"
#include "../../config/ConfigModels.h"
#include "../../gateway/GatewayHost.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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
			SkillsFacade& skillsFacade;
			SkillSecurityScanService& securityScanService;
			SkillsWatchService& watchService;

			SkillsCatalogSnapshot& catalog; // Keep coordinator context consistent
			SkillsEligibilitySnapshot& eligibility; // Keep coordinator context consistent
			HookCatalogSnapshot& hookCatalog;
			HookExecutionSnapshot& hookExecution;
			SkillsPromptSnapshot& prompt;
			HookEventSnapshot& events;
			SkillsCommandSnapshot& commands;
			SkillsSyncSnapshot& sync;
			SkillsEnvOverrideSnapshot& envOverrides;
			SkillsInstallSnapshot& install;
			SkillsRunSnapshot& runSnapshot;
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

		struct HookBootstrapProjectionContext {
			const std::vector<HookBootstrapFile>& bootstrapFiles;
			std::wstring& prompt;
			std::uint32_t& promptChars;
			bool& promptTruncated;
			std::size_t maxSkillsPromptChars = 0;
			std::wstring& lastReminderState;
			std::wstring& lastReminderReason;
			bool& selfEvolvingHookTriggered;
		};

		void RefreshSkillsState(
			const blazeclaw::config::AppConfig& config,
			bool forceRefresh,
			const std::wstring& reason,
			const RefreshContext& context) const;

		blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState(
			const GatewayStateContext& context,
			const EntryBuilder& entryBuilder,
			const std::function<std::string(const std::wstring&)>& toNarrow) const;

		void EmitGovernanceAndRemediation(
			const HooksGovernanceEmitter::GovernanceContext& governanceContext,
			const HooksGovernanceEmitter::RemediationContext& remediationContext,
			std::vector<std::wstring>& inOutWarnings) const;

		void ApplyHookBootstrapProjection(
			HookBootstrapProjectionContext& context) const;

		[[nodiscard]] static bool ContainsBootstrapFile(
			const std::vector<HookBootstrapFile>& files,
			const std::wstring& expectedPath);
	};

} // namespace blazeclaw::core
