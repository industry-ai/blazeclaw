#pragma once

#include "../../config/ConfigModels.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core::bootstrap {

	class StartupPolicyResolver {
	public:
		struct RuntimeQueueSettings {
			bool asyncQueueEnabled = true;
			std::uint64_t queueWaitTimeoutMs = 15000;
			std::uint64_t executionTimeoutMs = 120000;
		};

		struct RuntimeOrchestrationPolicySettings {
			bool localModelStartupLoadEnabled = false;
			bool startupSkillsRefreshEnabled = false;
			bool startupHookBootstrapEnabled = false;
			bool startupFixtureValidationEnabled = false;
			std::vector<std::string> dynamicLoopCanaryProviders;
			std::vector<std::string> dynamicLoopCanarySessions;
			std::uint64_t dynamicLoopPromotionMinRuns = 20;
			double dynamicLoopPromotionMinSuccessRate = 0.95;
		};

		struct ToolRuntimePolicySettings {
			std::optional<std::filesystem::path> imapSmtpSkillRoot;
			std::optional<std::filesystem::path> baiduSearchSkillRoot;
			std::optional<std::filesystem::path> braveSearchSkillRoot;
			std::optional<std::filesystem::path> openClawWebBrowsingSkillRoot;
			bool braveRequireApiKey = false;
			bool braveApiKeyPresent = false;
			bool enableOpenClawWebBrowsingFallback = false;
		};

		struct EmailPolicySettings {
			std::wstring rolloutMode = L"legacy";
			std::wstring enforceChannel;
			bool rollbackBridgeEnabled = true;
			bool canaryEligible = false;
			bool runtimeEnabled = false;
			bool runtimeEnforce = false;
		};

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

		[[nodiscard]] RuntimeQueueSettings ResolveRuntimeQueueSettings(
			std::uint64_t defaultQueueWaitTimeoutMs,
			std::uint64_t defaultExecutionTimeoutMs) const;

		[[nodiscard]] RuntimeOrchestrationPolicySettings
			ResolveRuntimeOrchestrationPolicySettings() const;

		[[nodiscard]] EmailPolicySettings ResolveEmailPolicySettings(
			const blazeclaw::config::AppConfig& config) const;

		[[nodiscard]] ToolRuntimePolicySettings ResolveToolRuntimePolicySettings() const;

		void AppendStartupTrace(const char* stage) const;
	};

} // namespace blazeclaw::core::bootstrap
