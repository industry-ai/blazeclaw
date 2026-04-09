#pragma once

#include "../../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core::bootstrap {

	class StartupPolicyResolver {
	public:
		struct HooksPolicySettings {
			bool engineEnabled = true;
			bool fallbackPromptInjection = false;
			bool reminderEnabled = true;
			std::wstring reminderVerbosity = L"normal";
			std::vector<std::wstring> allowedPackages;
			bool strictPolicyEnforcement = false;
			bool governanceReportingEnabled = true;
			std::filesystem::path governanceReportDir;
			bool autoRemediationEnabled = false;
			bool autoRemediationRequiresApproval = true;
			std::wstring autoRemediationApprovalToken;
			std::wstring autoRemediationTenantId = L"default";
			std::filesystem::path autoRemediationPlaybookDir;
			std::uint32_t autoRemediationTokenMaxAgeMinutes = 1440;
			bool remediationTelemetryEnabled = true;
			std::filesystem::path remediationTelemetryDir;
			bool remediationAuditEnabled = true;
			std::filesystem::path remediationAuditDir;
			std::uint32_t remediationSloMaxDriftDetected = 0;
			std::uint32_t remediationSloMaxPolicyBlocked = 0;
			bool complianceAttestationEnabled = true;
			std::filesystem::path complianceAttestationDir;
			bool enterpriseSlaGovernanceEnabled = true;
			std::wstring enterpriseSlaPolicyId = L"default-policy";
			bool crossTenantAttestationAggregationEnabled = true;
			std::filesystem::path crossTenantAttestationAggregationDir;
		};

		[[nodiscard]] HooksPolicySettings ResolveHooksPolicySettings(
			const blazeclaw::config::AppConfig& config) const;

		void AppendStartupTrace(const char* stage) const;
	};

} // namespace blazeclaw::core::bootstrap
