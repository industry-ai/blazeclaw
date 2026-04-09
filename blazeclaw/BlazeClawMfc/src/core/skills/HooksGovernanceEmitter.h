#pragma once

#include "../HookExecutionService.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core {

	class HooksGovernanceEmitter {
	public:
		struct GovernanceContext {
			const HookExecutionSnapshot& execution;
			bool governanceReportingEnabled = false;
			std::filesystem::path governanceReportDir;
			std::vector<std::wstring> allowedPackages;
			bool strictPolicyEnforcement = false;
			std::uint64_t& governanceReportsGenerated;
			std::wstring& lastGovernanceReportPath;
		};

		struct RemediationContext {
			const HookExecutionSnapshot& execution;
			bool autoRemediationEnabled = false;
			std::wstring autoRemediationTenantId;
			std::filesystem::path autoRemediationPlaybookDir;
			std::uint32_t autoRemediationTokenMaxAgeMinutes = 1440;
			std::wstring autoRemediationApprovalToken;
			std::uint64_t autoRemediationTokenRotations = 0;

			bool remediationTelemetryEnabled = false;
			std::filesystem::path remediationTelemetryDir;
			bool remediationAuditEnabled = false;
			std::filesystem::path remediationAuditDir;

			std::uint32_t remediationSloMaxDriftDetected = 0;
			std::uint32_t remediationSloMaxPolicyBlocked = 0;

			bool complianceAttestationEnabled = false;
			std::filesystem::path complianceAttestationDir;

			std::wstring enterpriseSlaPolicyId;
			bool crossTenantAttestationAggregationEnabled = false;
			std::filesystem::path crossTenantAttestationAggregationDir;

			std::wstring& lastGovernanceReportPath;
			std::wstring& lastAutoRemediationPlaybookPath;
			std::wstring& lastAutoRemediationStatus;
			std::wstring& lastRemediationTelemetryPath;
			std::wstring& lastRemediationAuditPath;
			std::wstring& remediationSloStatus;
			std::wstring& lastComplianceAttestationPath;
			std::uint64_t& crossTenantAttestationAggregationCount;
			std::wstring& crossTenantAttestationAggregationStatus;
			std::wstring& lastCrossTenantAttestationAggregationPath;
		};

		void EmitGovernanceReportIfNeeded(
			const GovernanceContext& context,
			std::vector<std::wstring>& inOutWarnings) const;

		void EmitRemediationLifecycleIfNeeded(
			const RemediationContext& context,
			std::vector<std::wstring>& inOutWarnings) const;
	};

} // namespace blazeclaw::core
