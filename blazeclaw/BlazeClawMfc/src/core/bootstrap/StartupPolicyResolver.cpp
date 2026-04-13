#include "pch.h"
#include "StartupPolicyResolver.h"
#include "StartupFixtureValidator.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <Windows.h>

namespace blazeclaw::core::bootstrap {

	namespace {

		std::optional<std::filesystem::path> ResolveSkillRootFromSearchPaths(
			const std::vector<std::filesystem::path>& suffix,
			const std::vector<std::filesystem::path>& requiredScripts)
		{
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(std::filesystem::current_path());

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
			{
				candidates.push_back(std::filesystem::path(modulePath).parent_path());
			}

			for (const auto& root : candidates)
			{
				std::filesystem::path cursor = root;
				while (!cursor.empty())
				{
					std::filesystem::path candidate = cursor;
					for (const auto& segment : suffix)
					{
						candidate /= segment;
					}

					bool allPresent = true;
					for (const auto& script : requiredScripts)
					{
						if (!std::filesystem::exists(candidate / script))
						{
							allPresent = false;
							break;
						}
					}

					if (allPresent)
					{
						return candidate;
					}

					if (!cursor.has_parent_path())
					{
						break;
					}

					auto parent = cursor.parent_path();
					if (parent == cursor)
					{
						break;
					}

					cursor = parent;
				}
			}

			return std::nullopt;
		}

		std::wstring TrimWide(const std::wstring& value)
		{
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; });
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; })
				.base();

			if (first >= last)
			{
				return {};
			}

			return std::wstring(first, last);
		}

		bool HasEnvVarValue(const wchar_t* key)
		{
			wchar_t* value = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&value, &len, key) != 0 || value == nullptr || len == 0)
			{
				if (value != nullptr)
				{
					free(value);
				}
				return false;
			}

			const std::wstring trimmed = TrimWide(value);
			free(value);
			return !trimmed.empty();
		}

		bool ReadBoolEnvOrDefault(const wchar_t* key, const bool fallback)
		{
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 || value == nullptr ||
				length == 0)
			{
				if (value != nullptr)
				{
					free(value);
				}

				return fallback;
			}

			std::wstring normalized;
			normalized.reserve(length);
			for (std::size_t i = 0; i < length && value[i] != L'\0'; ++i)
			{
				normalized.push_back(static_cast<wchar_t>(std::towlower(value[i])));
			}
			free(value);

			if (normalized == L"1" || normalized == L"true" || normalized == L"yes" ||
				normalized == L"on")
			{
				return true;
			}

			if (normalized == L"0" || normalized == L"false" || normalized == L"no" ||
				normalized == L"off")
			{
				return false;
			}

			return fallback;
		}

		std::wstring ReadStringEnvOrDefault(
			const wchar_t* key,
			const std::wstring& fallback)
		{
			std::wstring value = fallback;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) == 0 && env != nullptr && len > 0)
			{
				value.assign(env);
			}
			if (env != nullptr)
			{
				free(env);
			}

			return TrimWide(value);
		}

		std::filesystem::path ReadPathEnvOrDefault(
			const wchar_t* key,
			const std::wstring& fallback)
		{
			const auto trimmed = ReadStringEnvOrDefault(key, fallback);
			if (trimmed.empty())
			{
				return std::filesystem::path(fallback);
			}

			return std::filesystem::path(trimmed);
		}

		std::uint32_t ReadUInt32EnvOrDefault(
			const wchar_t* key,
			const std::uint32_t fallback)
		{
			wchar_t* env = nullptr;
			std::size_t len = 0;
			std::uint32_t value = fallback;
			if (_wdupenv_s(&env, &len, key) == 0 && env != nullptr && len > 0)
			{
				const std::wstring trimmed = TrimWide(env);
				if (!trimmed.empty())
				{
					std::wistringstream parser(trimmed);
					std::uint32_t parsed = 0;
					if ((parser >> parsed) && parser.eof())
					{
						value = parsed;
					}
				}
			}
			if (env != nullptr)
			{
				free(env);
			}

			return value;
		}

		std::uint64_t ParseUInt64EnvValue(
			const wchar_t* key,
			const std::uint64_t fallback) {
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return fallback;
			}

			const std::wstring rawValue(env);
			free(env);

			const std::wstring trimmed = TrimWide(rawValue);
			if (trimmed.empty()) {
				return fallback;
			}

			try {
				return static_cast<std::uint64_t>(std::stoull(trimmed));
			}
			catch (...) {
				return fallback;
			}
		}

		double ParseDoubleEnvValue(
			const wchar_t* key,
			const double fallback) {
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return fallback;
			}

			const std::wstring rawValue(env);
			free(env);

			const std::wstring trimmed = TrimWide(rawValue);
			if (trimmed.empty()) {
				return fallback;
			}

			try {
				return std::stod(trimmed);
			}
			catch (...) {
				return fallback;
			}
		}

		std::vector<std::string> ParseCsvEnvValues(const wchar_t* key) {
			std::vector<std::string> values;
			auto toNarrowAscii = [](const std::wstring& input) {
				std::string output;
				output.reserve(input.size());
				for (const wchar_t ch : input) {
					output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
				}
				return output;
				};

			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return values;
			}

			std::wstring token;
			for (std::size_t i = 0; i < len && env[i] != L'\0'; ++i) {
				if (env[i] == L',' || env[i] == L';') {
					const auto trimmed = TrimWide(token);
					if (!trimmed.empty()) {
						values.push_back(toNarrowAscii(trimmed));
					}
					token.clear();
					continue;
				}

				token.push_back(env[i]);
			}

			const auto trimmed = TrimWide(token);
			if (!trimmed.empty()) {
				values.push_back(toNarrowAscii(trimmed));
			}

			free(env);
			return values;
		}

		std::wstring NormalizeReminderVerbosity(const std::wstring& value)
		{
			std::wstring normalized;
			normalized.reserve(value.size());
			for (const wchar_t ch : value)
			{
				normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
			}

			if (normalized == L"minimal" || normalized == L"normal" ||
				normalized == L"detailed")
			{
				return normalized;
			}

			return L"normal";
		}

		std::vector<std::wstring> ResolveAllowedPackages(
			const blazeclaw::config::AppConfig& config)
		{
			std::vector<std::wstring> values;
			const auto addNormalized = [&values](const std::wstring& raw)
				{
					const auto trimmed = TrimWide(raw);
					if (trimmed.empty())
					{
						return;
					}

					std::wstring lowered;
					lowered.reserve(trimmed.size());
					for (const auto ch : trimmed)
					{
						lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
					}
					values.push_back(lowered);
				};

			for (const auto& item : config.hooks.engine.allowedPackages)
			{
				addNormalized(item);
			}

			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, L"BLAZECLAW_HOOKS_ALLOWED_PACKAGES") == 0 &&
				env != nullptr && len > 0)
			{
				std::wstring token;
				for (std::size_t i = 0; i < len && env[i] != L'\0'; ++i)
				{
					if (env[i] == L',' || env[i] == L';')
					{
						addNormalized(token);
						token.clear();
					}
					else
					{
						token.push_back(env[i]);
					}
				}
				addNormalized(token);
			}
			if (env != nullptr)
			{
				free(env);
			}

			std::sort(values.begin(), values.end());
			values.erase(std::unique(values.begin(), values.end()), values.end());
			return values;
		}

	} // namespace

	StartupPolicyResolver::HooksPolicySettings
		StartupPolicyResolver::ResolveHooksPolicySettings(
			const blazeclaw::config::AppConfig& config) const
	{
		HooksPolicySettings settings;
		settings.engineEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_ENGINE_ENABLED",
			config.hooks.engine.enabled);
		settings.fallbackPromptInjection = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_FALLBACK_PROMPT_INJECTION",
			config.hooks.engine.fallbackPromptInjection);
		settings.reminderEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_REMINDER_ENABLED",
			config.hooks.engine.reminderEnabled);
		settings.reminderVerbosity = NormalizeReminderVerbosity(
			ReadStringEnvOrDefault(
				L"BLAZECLAW_HOOKS_REMINDER_VERBOSITY",
				config.hooks.engine.reminderVerbosity));
		settings.allowedPackages = ResolveAllowedPackages(config);
		settings.strictPolicyEnforcement = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_STRICT_POLICY_ENFORCEMENT",
			config.hooks.engine.strictPolicyEnforcement);
		settings.governanceReportingEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_GOVERNANCE_REPORTING_ENABLED",
			config.hooks.engine.governanceReportingEnabled);
		settings.governanceReportDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_GOVERNANCE_REPORT_DIR",
			config.hooks.engine.governanceReportDir.empty()
			? L"blazeclaw/reports/hooks-governance"
			: config.hooks.engine.governanceReportDir);
		settings.autoRemediationEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_ENABLED",
			config.hooks.engine.autoRemediationEnabled);
		settings.autoRemediationRequiresApproval = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_REQUIRES_APPROVAL",
			config.hooks.engine.autoRemediationRequiresApproval);
		settings.autoRemediationApprovalToken = ReadStringEnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_APPROVAL_TOKEN",
			config.hooks.engine.autoRemediationApprovalToken);
		const auto tenantId = ReadStringEnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_TENANT_ID",
			config.hooks.engine.autoRemediationTenantId);
		settings.autoRemediationTenantId = tenantId.empty() ? L"default" : tenantId;
		settings.autoRemediationPlaybookDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_PLAYBOOK_DIR",
			config.hooks.engine.autoRemediationPlaybookDir.empty()
			? L"blazeclaw/reports/hooks-remediation-playbooks"
			: config.hooks.engine.autoRemediationPlaybookDir);
		settings.autoRemediationTokenMaxAgeMinutes = ReadUInt32EnvOrDefault(
			L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_TOKEN_MAX_AGE_MINUTES",
			config.hooks.engine.autoRemediationTokenMaxAgeMinutes);
		if (settings.autoRemediationTokenMaxAgeMinutes == 0)
		{
			settings.autoRemediationTokenMaxAgeMinutes = 1440;
		}
		settings.remediationTelemetryEnabled = ReadBoolEnvOrDefault(
			L"BLAZENCLAW_HOOKS_REMEDIATION_TELEMETRY_ENABLED",
			config.hooks.engine.remediationTelemetryEnabled);
		settings.remediationTelemetryDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_REMEDIATION_TELEMETRY_DIR",
			config.hooks.engine.remediationTelemetryDir.empty()
			? L"blazeclaw/reports/hooks-remediation-telemetry"
			: config.hooks.engine.remediationTelemetryDir);
		settings.remediationAuditEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_REMEDIATION_AUDIT_ENABLED",
			config.hooks.engine.remediationAuditEnabled);
		settings.remediationAuditDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_REMEDIATION_AUDIT_DIR",
			config.hooks.engine.remediationAuditDir.empty()
			? L"blazeclaw/reports/hooks-remediation-audit"
			: config.hooks.engine.remediationAuditDir);
		settings.remediationSloMaxDriftDetected = ReadUInt32EnvOrDefault(
			L"BLAZECLAW_HOOKS_REMEDIATION_SLO_MAX_DRIFT_DETECTED",
			config.hooks.engine.remediationSloMaxDriftDetected);
		settings.remediationSloMaxPolicyBlocked = ReadUInt32EnvOrDefault(
			L"BLAZECLAW_HOOKS_REMEDIATION_SLO_MAX_POLICY_BLOCKED",
			config.hooks.engine.remediationSloMaxPolicyBlocked);
		settings.complianceAttestationEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_COMPLIANCE_ATTESTATION_ENABLED",
			config.hooks.engine.complianceAttestationEnabled);
		settings.complianceAttestationDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_COMPLIANCE_ATTESTATION_DIR",
			config.hooks.engine.complianceAttestationDir.empty()
			? L"blazeclaw/reports/hooks-remediation-attestation"
			: config.hooks.engine.complianceAttestationDir);
		settings.enterpriseSlaGovernanceEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_ENTERPRISE_SLA_GOVERNANCE_ENABLED",
			config.hooks.engine.enterpriseSlaGovernanceEnabled);
		const auto policyId = ReadStringEnvOrDefault(
			L"BLAZECLAW_HOOKS_ENTERPRISE_SLA_POLICY_ID",
			config.hooks.engine.enterpriseSlaPolicyId);
		settings.enterpriseSlaPolicyId = policyId.empty() ? L"default-policy" : policyId;
		settings.crossTenantAttestationAggregationEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_CROSS_TENANT_ATTESTATION_AGGREGATION_ENABLED",
			config.hooks.engine.crossTenantAttestationAggregationEnabled);
		settings.crossTenantAttestationAggregationDir = ReadPathEnvOrDefault(
			L"BLAZECLAW_HOOKS_CROSS_TENANT_ATTESTATION_AGGREGATION_DIR",
			config.hooks.engine.crossTenantAttestationAggregationDir.empty()
			? L"blazeclaw/reports/hooks-attestation-aggregation"
			: config.hooks.engine.crossTenantAttestationAggregationDir);

		return settings;
	}

	StartupPolicyResolver::RuntimeQueueSettings
		StartupPolicyResolver::ResolveRuntimeQueueSettings(
			const std::uint64_t defaultQueueWaitTimeoutMs,
			const std::uint64_t defaultExecutionTimeoutMs) const {
		RuntimeQueueSettings settings;
		settings.asyncQueueEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_CHAT_RUNTIME_ASYNC_QUEUE_ENABLED",
			true);
		settings.queueWaitTimeoutMs = ParseUInt64EnvValue(
			L"BLAZECLAW_CHAT_RUNTIME_QUEUE_WAIT_TIMEOUT_MS",
			defaultQueueWaitTimeoutMs);
		settings.executionTimeoutMs = ParseUInt64EnvValue(
			L"BLAZECLAW_CHAT_RUNTIME_EXECUTION_TIMEOUT_MS",
			defaultExecutionTimeoutMs);

		return settings;
	}

	StartupPolicyResolver::RuntimeOrchestrationPolicySettings
		StartupPolicyResolver::ResolveRuntimeOrchestrationPolicySettings() const {
		RuntimeOrchestrationPolicySettings settings;
		settings.localModelStartupLoadEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_LOCALMODEL_STARTUP_LOAD_ENABLED",
			false);
		settings.startupSkillsRefreshEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_SKILLS_STARTUP_REFRESH_ENABLED",
			false);
		settings.startupHookBootstrapEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_STARTUP_BOOTSTRAP_ENABLED",
			false);
		settings.startupFixtureValidationEnabled = ReadBoolEnvOrDefault(
			StartupFixtureValidator::kEnvStartupValidationEnabled,
			false);
		settings.dynamicLoopCanaryProviders = ParseCsvEnvValues(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_PROVIDERS");
		settings.dynamicLoopCanarySessions = ParseCsvEnvValues(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_SESSIONS");
		settings.dynamicLoopPromotionMinRuns = ParseUInt64EnvValue(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_RUNS",
			20);
		settings.dynamicLoopPromotionMinSuccessRate = ParseDoubleEnvValue(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_SUCCESS_RATE",
			0.95);

		return settings;
	}

	StartupPolicyResolver::EmailPolicySettings
		StartupPolicyResolver::ResolveEmailPolicySettings(
			const blazeclaw::config::AppConfig& config) const
	{
		EmailPolicySettings settings;
		settings.rolloutMode = config.email.policyProfiles.rolloutMode;
		if (settings.rolloutMode.empty())
		{
			settings.rolloutMode = L"legacy";
		}

		settings.enforceChannel = config.email.policyProfiles.enforceChannel;
		settings.rollbackBridgeEnabled =
			config.email.policyProfiles.rollbackBridgeEnabled;
		settings.canaryEligible = settings.enforceChannel.empty();
		if (!settings.canaryEligible)
		{
			for (const auto& channel : config.enabledChannels)
			{
				if (_wcsicmp(channel.c_str(), settings.enforceChannel.c_str()) == 0)
				{
					settings.canaryEligible = true;
					break;
				}
			}
		}

		settings.runtimeEnabled = config.email.policyProfiles.enabled;
		settings.runtimeEnforce = config.email.policyProfiles.enforce;
		if (_wcsicmp(settings.rolloutMode.c_str(), L"monitor") == 0)
		{
			settings.runtimeEnabled = true;
			settings.runtimeEnforce = false;
		}
		else if (_wcsicmp(settings.rolloutMode.c_str(), L"enforce") == 0)
		{
			settings.runtimeEnabled = true;
			settings.runtimeEnforce = settings.canaryEligible;
		}

		if (!settings.rollbackBridgeEnabled)
		{
			settings.runtimeEnabled = config.email.policyProfiles.enabled;
			settings.runtimeEnforce = config.email.policyProfiles.enforce;
		}

		return settings;
	}

	StartupPolicyResolver::ToolRuntimePolicySettings
		StartupPolicyResolver::ResolveToolRuntimePolicySettings() const
	{
		ToolRuntimePolicySettings settings;
		settings.imapSmtpSkillRoot = ResolveSkillRootFromSearchPaths(
			{ L"blazeclaw", L"skills", L"imap-smtp-email" },
			{ std::filesystem::path(L"scripts") / L"imap.js",
			  std::filesystem::path(L"scripts") / L"smtp.js" });
		settings.baiduSearchSkillRoot = ResolveSkillRootFromSearchPaths(
			{ L"blazeclaw", L"skills", L"baidu-search" },
			{ std::filesystem::path(L"scripts") / L"search.py" });
		settings.braveSearchSkillRoot = ResolveSkillRootFromSearchPaths(
			{ L"blazeclaw", L"skills", L"brave-search" },
			{ std::filesystem::path(L"scripts") / L"search.js",
			  std::filesystem::path(L"scripts") / L"content.js" });
		settings.openClawWebBrowsingSkillRoot = ResolveSkillRootFromSearchPaths(
			{ L"blazeclaw", L"skills-openclaw-original", L"web-browsing" },
			{ std::filesystem::path(L"scripts") / L"search_web.py" });
		if (!settings.openClawWebBrowsingSkillRoot.has_value())
		{
			settings.openClawWebBrowsingSkillRoot = ResolveSkillRootFromSearchPaths(
				{ L"skills-openclaw-original", L"web-browsing" },
				{ std::filesystem::path(L"scripts") / L"search_web.py" });
		}

		settings.braveRequireApiKey = ReadBoolEnvOrDefault(
			L"BLAZECLAW_BRAVE_REQUIRE_API_KEY",
			false);
		settings.braveApiKeyPresent = HasEnvVarValue(L"BRAVE_API_KEY");
		settings.enableOpenClawWebBrowsingFallback = ReadBoolEnvOrDefault(
			L"BLAZECLAW_WEB_BROWSING_ENABLE_OPENCLAW_FALLBACK",
			false);
		return settings;
	}

	void StartupPolicyResolver::AppendStartupTrace(const char* stage) const
	{
		if (stage == nullptr || *stage == '\0')
		{
			return;
		}

		wchar_t tempPath[MAX_PATH]{};
		const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
		std::filesystem::path logPath;
		if (tempLength > 0 && tempLength < MAX_PATH)
		{
			logPath = std::filesystem::path(tempPath) / L"BlazeClaw.startup.trace.log";
		}
		else
		{
			logPath = std::filesystem::current_path() / L"BlazeClaw.startup.trace.log";
		}

		std::ofstream output(logPath, std::ios::app);
		if (!output.is_open())
		{
			return;
		}

		output
			<< "pid=" << static_cast<unsigned long>(GetCurrentProcessId())
			<< " tick=" << static_cast<unsigned long long>(GetTickCount64())
			<< " stage=" << stage
			<< "\n";
	}

} // namespace blazeclaw::core::bootstrap
