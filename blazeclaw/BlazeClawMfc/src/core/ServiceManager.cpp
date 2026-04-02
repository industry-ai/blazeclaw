#include "pch.h"
#include "ServiceManager.h"
#include "../app/CredentialStore.h"

#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

namespace blazeclaw::core {

	namespace {

		std::string ToNarrow(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());

			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		std::wstring ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int needed = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (needed <= 0) {
				return {};
			}

			std::wstring output(static_cast<std::size_t>(needed), L'\0');
			MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				needed);
			return output;
		}

		std::string EscapeJsonUtf8(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		void EmitDeepSeekDiagnostic(
			const char* stage,
			const std::string& detail) {
			const std::string safeStage =
				(stage == nullptr || std::string(stage).empty())
				? "unknown"
				: std::string(stage);
			TRACE(
				"[DeepSeek][%s] %s\n",
				safeStage.c_str(),
				detail.c_str());
		}

		std::string NormalizeDeepSeekApiModelId(
			const std::string& modelId) {
			if (modelId.empty() || modelId == "deepseek") {
				return "deepseek-chat";
			}

			if (modelId == "deepseek/deepseek-chat") {
				return "deepseek-chat";
			}

			if (modelId == "deepseek/deepseek-reasoner") {
				return "deepseek-reasoner";
			}

			return modelId;
		}

		std::optional<std::string> ParseHttpsUrl(
			const std::string& url,
			std::wstring& host,
			std::wstring& path,
			INTERNET_PORT& port,
			bool& secure) {
			host.clear();
			path.clear();
			port = INTERNET_DEFAULT_HTTPS_PORT;
			secure = true;

			const std::wstring urlW = ToWide(url);
			if (urlW.empty()) {
				return std::string("invalid_url");
			}

			URL_COMPONENTSW components{};
			components.dwStructSize = sizeof(components);
			components.dwHostNameLength = static_cast<DWORD>(-1);
			components.dwUrlPathLength = static_cast<DWORD>(-1);
			components.dwExtraInfoLength = static_cast<DWORD>(-1);

			if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &components)) {
				return std::string("invalid_url");
			}

			if (components.nScheme != INTERNET_SCHEME_HTTPS) {
				return std::string("deepseek_https_required");
			}

			secure = true;
			port = components.nPort;
			host.assign(components.lpszHostName, components.dwHostNameLength);
			path.assign(components.lpszUrlPath, components.dwUrlPathLength);
			if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr) {
				path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
			}
			if (path.empty()) {
				path = L"/";
			}

			return std::nullopt;
		}

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; });
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; })
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		bool ReadBoolEnvOrDefault(const wchar_t* key, const bool fallback) {
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 || value == nullptr ||
				length == 0) {
				if (value != nullptr) {
					free(value);
				}

				return fallback;
			}

			std::wstring normalized;
			normalized.reserve(length);
			for (std::size_t i = 0; i < length && value[i] != L'\0'; ++i) {
				normalized.push_back(static_cast<wchar_t>(std::towlower(value[i])));
			}
			free(value);

			if (normalized == L"1" || normalized == L"true" || normalized == L"yes" ||
				normalized == L"on") {
				return true;
			}

			if (normalized == L"0" || normalized == L"false" || normalized == L"no" ||
				normalized == L"off") {
				return false;
			}

			return fallback;
		}

		std::vector<std::wstring> ResolveHooksAllowedPackages(
			const blazeclaw::config::AppConfig& config) {
			std::vector<std::wstring> values;

			const auto addNormalized = [&values](const std::wstring& raw) {
				const auto trimmed = Trim(raw);
				if (trimmed.empty()) {
					return;
				}

				std::wstring lowered;
				lowered.reserve(trimmed.size());
				for (const auto ch : trimmed) {
					lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
				}
				values.push_back(lowered);
				};

			for (const auto& item : config.hooks.engine.allowedPackages) {
				addNormalized(item);
			}

			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, L"BLAZECLAW_HOOKS_ALLOWED_PACKAGES") == 0 &&
				env != nullptr && len > 0) {
				std::wstring token;
				for (std::size_t i = 0; i < len && env[i] != L'\0'; ++i) {
					if (env[i] == L',' || env[i] == L';') {
						addNormalized(token);
						token.clear();
					}
					else {
						token.push_back(env[i]);
					}
				}
				addNormalized(token);
			}
			if (env != nullptr) {
				free(env);
			}

			std::sort(values.begin(), values.end());
			values.erase(std::unique(values.begin(), values.end()), values.end());
			return values;
		}

		bool ResolveHooksStrictPolicyEnforcement(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_STRICT_POLICY_ENFORCEMENT",
				config.hooks.engine.strictPolicyEnforcement);
		}

		bool ResolveHooksAutoRemediationEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_ENABLED",
				config.hooks.engine.autoRemediationEnabled);
		}

		bool ResolveHooksAutoRemediationRequiresApproval(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_REQUIRES_APPROVAL",
				config.hooks.engine.autoRemediationRequiresApproval);
		}

		std::wstring ResolveHooksAutoRemediationApprovalToken(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.autoRemediationApprovalToken;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_APPROVAL_TOKEN") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			return Trim(value);
		}

		std::wstring ResolveHooksAutoRemediationTenantId(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.autoRemediationTenantId;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_TENANT_ID") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			return trimmed.empty() ? L"default" : trimmed;
		}

		std::filesystem::path ResolveHooksAutoRemediationPlaybookDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.autoRemediationPlaybookDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_PLAYBOOK_DIR") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-remediation-playbooks");
			}

			return std::filesystem::path(trimmed);
		}

		bool ResolveHooksEnterpriseSlaGovernanceEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_ENTERPRISE_SLA_GOVERNANCE_ENABLED",
				config.hooks.engine.enterpriseSlaGovernanceEnabled);
		}

		std::wstring ResolveHooksEnterpriseSlaPolicyId(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.enterpriseSlaPolicyId;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_ENTERPRISE_SLA_POLICY_ID") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			return trimmed.empty() ? L"default-policy" : trimmed;
		}

		bool ResolveHooksCrossTenantAttestationAggregationEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_CROSS_TENANT_ATTESTATION_AGGREGATION_ENABLED",
				config.hooks.engine.crossTenantAttestationAggregationEnabled);
		}

		std::filesystem::path ResolveHooksCrossTenantAttestationAggregationDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.crossTenantAttestationAggregationDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_CROSS_TENANT_ATTESTATION_AGGREGATION_DIR") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-attestation-aggregation");
			}

			return std::filesystem::path(trimmed);
		}

		std::uint32_t ResolveHooksRemediationSloMaxDriftDetected(
			const blazeclaw::config::AppConfig& config) {
			std::uint32_t value = config.hooks.engine.remediationSloMaxDriftDetected;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_REMEDIATION_SLO_MAX_DRIFT_DETECTED") == 0 &&
				env != nullptr &&
				len > 0) {
				std::wstring trimmedEnv = Trim(env);
				if (!trimmedEnv.empty()) {
					std::wistringstream parser(trimmedEnv);
					std::uint32_t parsed = 0;
					if ((parser >> parsed) && parser.eof()) {
						value = parsed;
					}
				}
			}
			if (env != nullptr) {
				free(env);
			}

			return value;
		}

		std::uint32_t ResolveHooksRemediationSloMaxPolicyBlocked(
			const blazeclaw::config::AppConfig& config) {
			std::uint32_t value = config.hooks.engine.remediationSloMaxPolicyBlocked;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_REMEDIATION_SLO_MAX_POLICY_BLOCKED") == 0 &&
				env != nullptr &&
				len > 0) {
				std::wstring trimmedEnv = Trim(env);
				if (!trimmedEnv.empty()) {
					std::wistringstream parser(trimmedEnv);
					std::uint32_t parsed = 0;
					if ((parser >> parsed) && parser.eof()) {
						value = parsed;
					}
				}
			}
			if (env != nullptr) {
				free(env);
			}

			return value;
		}

		bool ResolveHooksComplianceAttestationEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_COMPLIANCE_ATTESTATION_ENABLED",
				config.hooks.engine.complianceAttestationEnabled);
		}

		std::filesystem::path ResolveHooksComplianceAttestationDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.complianceAttestationDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_COMPLIANCE_ATTESTATION_DIR") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-remediation-attestation");
			}

			return std::filesystem::path(trimmed);
		}

		std::uint32_t ResolveHooksAutoRemediationTokenMaxAgeMinutes(
			const blazeclaw::config::AppConfig& config) {
			std::uint32_t value = config.hooks.engine.autoRemediationTokenMaxAgeMinutes;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_AUTO_REMEDIATION_TOKEN_MAX_AGE_MINUTES") == 0 &&
				env != nullptr &&
				len > 0) {
				std::wstring trimmedEnv = Trim(env);
				if (!trimmedEnv.empty()) {
					std::wistringstream parser(trimmedEnv);
					std::uint32_t parsed = 0;
					if ((parser >> parsed) && parser.eof()) {
						value = parsed;
					}
				}
			}
			if (env != nullptr) {
				free(env);
			}

			return value == 0 ? 1440 : value;
		}

		std::wstring EvaluateRemediationSloStatus(
			const HookExecutionSnapshot& execution,
			const std::uint32_t maxDriftDetected,
			const std::uint32_t maxPolicyBlocked) {
			if (execution.diagnostics.driftDetectedCount > maxDriftDetected) {
				return L"breach_drift";
			}

			if (execution.diagnostics.policyBlockedCount > maxPolicyBlocked) {
				return L"breach_policy_blocked";
			}

			return L"healthy";
		}

		std::wstring BuildComplianceAttestationJson(
			const std::wstring& tenantId,
			const std::wstring& sloStatus,
			const HookExecutionSnapshot& execution,
			const std::wstring& telemetryPath,
			const std::wstring& auditPath) {
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"sloStatus\":\"" << sloStatus
				<< L"\",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"telemetryPath\":\"" << telemetryPath
				<< L"\",\"auditPath\":\"" << auditPath
				<< L"\"}";
			return builder.str();
		}

		void EmitComplianceAttestationIfNeeded(
			const bool enabled,
			const std::filesystem::path& attestationDir,
			const std::wstring& tenantId,
			const std::wstring& sloStatus,
			const HookExecutionSnapshot& execution,
			const std::wstring& telemetryPath,
			const std::wstring& auditPath,
			std::wstring& outAttestationPath,
			std::vector<std::wstring>& inOutWarnings) {
			outAttestationPath.clear();
			if (!enabled) {
				return;
			}

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0) {
				return;
			}

			std::error_code ec;
			auto fullDir = attestationDir;
			if (fullDir.is_relative()) {
				fullDir = std::filesystem::current_path(ec) / fullDir;
			}
			std::filesystem::create_directories(fullDir, ec);
			if (ec) {
				inOutWarnings.push_back(
					L"hooks-remediation compliance attestation emission failed: cannot create directory.");
				return;
			}

			const auto content = BuildComplianceAttestationJson(
				tenantId,
				sloStatus,
				execution,
				telemetryPath,
				auditPath);
			const auto nonce = std::to_wstring(
				static_cast<std::uint64_t>(
					std::chrono::system_clock::now().time_since_epoch().count()));
			const auto attestationPath =
				fullDir / (L"hooks-remediation-attestation-" + tenantId + L"-" + nonce + L".json");

			std::ofstream output(attestationPath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				inOutWarnings.push_back(
					L"hooks-remediation compliance attestation emission failed: cannot write file.");
				return;
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content) {
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			if (!output.good()) {
				inOutWarnings.push_back(
					L"hooks-remediation compliance attestation emission failed: cannot flush file.");
				return;
			}

			outAttestationPath = attestationPath.wstring();
		}

		std::wstring BuildCrossTenantAttestationAggregationJson(
			const std::wstring& tenantId,
			const std::wstring& policyId,
			const std::wstring& sloStatus,
			const std::wstring& attestationPath,
			const HookExecutionSnapshot& execution,
			const std::uint64_t aggregationCount) {
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"policyId\":\"" << policyId
				<< L"\",\"sloStatus\":\"" << sloStatus
				<< L"\",\"attestationPath\":\"" << attestationPath
				<< L"\",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"aggregationSequence\":"
				<< (aggregationCount + 1)
				<< L"}";
			return builder.str();
		}

		std::wstring WriteLifecycleArtifact(
			const std::filesystem::path& outputDir,
			const std::wstring& filePrefix,
			const std::wstring& tenantId,
			const std::wstring& content,
			const std::wstring& failureLabel,
			std::vector<std::wstring>& inOutWarnings);

		void EmitCrossTenantAttestationAggregationIfNeeded(
			const bool enabled,
			const std::filesystem::path& aggregationDir,
			const std::wstring& tenantId,
			const std::wstring& policyId,
			const std::wstring& sloStatus,
			const std::wstring& attestationPath,
			const HookExecutionSnapshot& execution,
			std::uint64_t& inOutAggregationCount,
			std::wstring& outAggregationStatus,
			std::wstring& outAggregationPath,
			std::vector<std::wstring>& inOutWarnings) {
			outAggregationPath.clear();
			if (!enabled) {
				outAggregationStatus = L"aggregation_disabled";
				return;
			}

			if (attestationPath.empty()) {
				outAggregationStatus = L"attestation_missing";
				return;
			}

			const auto content = BuildCrossTenantAttestationAggregationJson(
				tenantId,
				policyId,
				sloStatus,
				attestationPath,
				execution,
				inOutAggregationCount);
			const auto path = WriteLifecycleArtifact(
				aggregationDir,
				L"hooks-attestation-aggregation",
				tenantId,
				content,
				L"hooks-remediation cross-tenant aggregation emission failed",
				inOutWarnings);
			if (path.empty()) {
				outAggregationStatus = L"aggregation_emit_failed";
				return;
			}

			++inOutAggregationCount;
			outAggregationStatus = L"aggregation_emitted";
			outAggregationPath = path;
		}

		bool ResolveHooksRemediationTelemetryEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_REMEDIATION_TELEMETRY_ENABLED",
				config.hooks.engine.remediationTelemetryEnabled);
		}

		std::filesystem::path ResolveHooksRemediationTelemetryDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.remediationTelemetryDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_REMEDIATION_TELEMETRY_DIR") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-remediation-telemetry");
			}

			return std::filesystem::path(trimmed);
		}

		bool ResolveHooksRemediationAuditEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_REMEDIATION_AUDIT_ENABLED",
				config.hooks.engine.remediationAuditEnabled);
		}

		std::filesystem::path ResolveHooksRemediationAuditDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.remediationAuditDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(
				&env,
				&len,
				L"BLAZECLAW_HOOKS_REMEDIATION_AUDIT_DIR") == 0 &&
				env != nullptr &&
				len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-remediation-audit");
			}

			return std::filesystem::path(trimmed);
		}

		std::wstring BuildRemediationPlaybookJson(
			const std::wstring& tenantId,
			const HookExecutionSnapshot& execution,
			const std::wstring& reportPath,
			const std::uint32_t tokenMaxAgeMinutes) {
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\",\"sourceReportPath\":\""
				<< reportPath
				<< L"\",\"tokenMaxAgeMinutes\":"
				<< tokenMaxAgeMinutes
				<< L",\"recommendedSteps\":[\"validate_approval_token\",\"review_policy_drift\",\"execute_remediation_with_gate\"]}";
			return builder.str();
		}

		void EmitTenantRemediationPlaybookIfNeeded(
			const HookExecutionSnapshot& execution,
			const bool autoRemediationEnabled,
			const std::wstring& tenantId,
			const std::filesystem::path& playbookDir,
			const std::wstring& sourceReportPath,
			const std::uint32_t tokenMaxAgeMinutes,
			std::wstring& outPlaybookPath,
			std::vector<std::wstring>& inOutWarnings) {
			outPlaybookPath.clear();
			if (!autoRemediationEnabled) {
				return;
			}

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0) {
				return;
			}

			std::error_code ec;
			auto fullDir = playbookDir;
			if (fullDir.is_relative()) {
				fullDir = std::filesystem::current_path(ec) / fullDir;
			}

			std::filesystem::create_directories(fullDir, ec);
			if (ec) {
				inOutWarnings.push_back(
					L"hooks-remediation playbook generation failed: cannot create directory.");
				return;
			}

			const auto nonce = std::to_wstring(
				static_cast<std::uint64_t>(
					std::chrono::system_clock::now().time_since_epoch().count()));
			const auto playbookFile =
				fullDir / (L"hooks-remediation-" + tenantId + L"-" + nonce + L".json");
			const auto content = BuildRemediationPlaybookJson(
				tenantId,
				execution,
				sourceReportPath,
				tokenMaxAgeMinutes);
			std::ofstream output(playbookFile, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				inOutWarnings.push_back(
					L"hooks-remediation playbook generation failed: cannot write file.");
				return;
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content) {
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			if (!output.good()) {
				inOutWarnings.push_back(
					L"hooks-remediation playbook generation failed: cannot write file.");
				return;
			}

			outPlaybookPath = playbookFile.wstring();
		}

		std::wstring BuildRemediationTelemetryJson(
			const std::wstring& tenantId,
			const std::wstring& playbookPath,
			const HookExecutionSnapshot& execution,
			const std::wstring& remediationStatus) {
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"playbookPath\":\"" << playbookPath
				<< L"\",\"status\":\"" << remediationStatus
				<< L"\",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\"}";
			return builder.str();
		}

		std::wstring BuildRemediationAuditJson(
			const std::wstring& tenantId,
			const std::wstring& approvalToken,
			const std::wstring& remediationStatus,
			const std::wstring& reportPath,
			const std::wstring& playbookPath,
			const std::uint64_t tokenRotations) {
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"approvalTokenConfigured\":"
				<< (!approvalToken.empty() ? L"true" : L"false")
				<< L",\"status\":\"" << remediationStatus
				<< L"\",\"reportPath\":\"" << reportPath
				<< L"\",\"playbookPath\":\"" << playbookPath
				<< L"\",\"tokenRotations\":" << tokenRotations
				<< L"}";
			return builder.str();
		}

		std::wstring WriteLifecycleArtifact(
			const std::filesystem::path& outputDir,
			const std::wstring& filePrefix,
			const std::wstring& tenantId,
			const std::wstring& content,
			const std::wstring& failureLabel,
			std::vector<std::wstring>& inOutWarnings) {
			std::error_code ec;
			auto fullDir = outputDir;
			if (fullDir.is_relative()) {
				fullDir = std::filesystem::current_path(ec) / fullDir;
			}
			std::filesystem::create_directories(fullDir, ec);
			if (ec) {
				inOutWarnings.push_back(failureLabel + L": cannot create directory.");
				return {};
			}

			const auto nonce = std::to_wstring(
				static_cast<std::uint64_t>(
					std::chrono::system_clock::now().time_since_epoch().count()));
			const auto path = fullDir / (filePrefix + L"-" + tenantId + L"-" + nonce + L".json");
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				inOutWarnings.push_back(failureLabel + L": cannot write file.");
				return {};
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content) {
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			if (!output.good()) {
				inOutWarnings.push_back(failureLabel + L": cannot flush file.");
				return {};
			}

			return path.wstring();
		}

		void EmitRemediationTelemetryAndAuditIfNeeded(
			const HookExecutionSnapshot& execution,
			const bool telemetryEnabled,
			const std::filesystem::path& telemetryDir,
			const bool auditEnabled,
			const std::filesystem::path& auditDir,
			const std::wstring& tenantId,
			const std::wstring& approvalToken,
			const std::wstring& reportPath,
			const std::wstring& playbookPath,
			const std::wstring& remediationStatus,
			const std::uint64_t tokenRotations,
			std::wstring& outTelemetryPath,
			std::wstring& outAuditPath,
			std::vector<std::wstring>& inOutWarnings) {
			outTelemetryPath.clear();
			outAuditPath.clear();

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0) {
				return;
			}

			if (telemetryEnabled) {
				const auto telemetryContent = BuildRemediationTelemetryJson(
					tenantId,
					playbookPath,
					execution,
					remediationStatus);
				outTelemetryPath = WriteLifecycleArtifact(
					telemetryDir,
					L"hooks-remediation-telemetry",
					tenantId,
					telemetryContent,
					L"hooks-remediation telemetry emission failed",
					inOutWarnings);
			}

			if (auditEnabled) {
				const auto auditContent = BuildRemediationAuditJson(
					tenantId,
					approvalToken,
					remediationStatus,
					reportPath,
					playbookPath,
					tokenRotations);
				outAuditPath = WriteLifecycleArtifact(
					auditDir,
					L"hooks-remediation-audit",
					tenantId,
					auditContent,
					L"hooks-remediation audit emission failed",
					inOutWarnings);
			}
		}

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::string BuildAttachmentSummary(
			const std::vector<std::string>& attachmentMimeTypes) {
			std::string summary = "[attachments]";
			if (attachmentMimeTypes.empty()) {
				summary += "\n- image (mimeType=unknown)";
				return summary;
			}

			for (const auto& mimeType : attachmentMimeTypes) {
				summary += "\n- image (mimeType=";
				summary += mimeType.empty() ? "unknown" : mimeType;
				summary += ")";
			}

			return summary;
		}

		std::string BuildQwen3ChatPrompt(
			const std::string& userMessage,
			const bool hasAttachments,
			const std::vector<std::string>& attachmentMimeTypes,
			const bool strictNoEcho) {
			std::string normalizedUserMessage = userMessage;
			if (normalizedUserMessage.empty()) {
				normalizedUserMessage = "User sent image attachments.";
			}

			if (hasAttachments) {
				normalizedUserMessage += "\n\n";
				normalizedUserMessage += BuildAttachmentSummary(attachmentMimeTypes);
				normalizedUserMessage +=
					"\nInstruction: respond as a text assistant. "
					"Do not repeat the user message.";
			}

			std::string prompt;
			prompt.reserve(normalizedUserMessage.size() + 256);
			prompt += "<|im_start|>system\n";
			prompt += "You are a helpful assistant. Answer the user directly. ";
			prompt += "Do not echo the user prompt verbatim. ";
			if (strictNoEcho) {
				prompt += "Do not quote or repeat the user's wording. ";
				prompt += "Give only the helpful answer content. ";
			}
			prompt += "\n";
			prompt += "<|im_end|>\n";
			prompt += "<|im_start|>user\n";
			prompt += normalizedUserMessage;
			prompt += "\n<|im_end|>\n";
			prompt += "<|im_start|>assistant\n";
			return prompt;
		}

		std::string BuildLocalModelPrompt(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
			return BuildQwen3ChatPrompt(
				request.message,
				request.hasAttachments,
				request.attachmentMimeTypes,
				false);
		}

		std::string BuildLocalModelRetryPrompt(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
			return BuildQwen3ChatPrompt(
				request.message,
				request.hasAttachments,
				request.attachmentMimeTypes,
				true);
		}

		std::string TrimAsciiWhitespace(const std::string& value) {
			const std::size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}

			const std::size_t last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		std::string NormalizeForEchoCheck(const std::string& value) {
			const std::string trimmed = TrimAsciiWhitespace(value);
			std::string normalized;
			normalized.reserve(trimmed.size());

			bool previousWasSpace = false;
			for (const char ch : trimmed) {
				const unsigned char code = static_cast<unsigned char>(ch);
				if (std::isspace(code) != 0) {
					if (!previousWasSpace) {
						normalized.push_back(' ');
						previousWasSpace = true;
					}
					continue;
				}

				if (std::ispunct(code) != 0) {
					continue;
				}

				normalized.push_back(
					static_cast<char>(std::tolower(code)));
				previousWasSpace = false;
			}

			return TrimAsciiWhitespace(normalized);
		}

		bool IsLikelyEchoResponse(
			const std::string& userMessage,
			const std::string& assistantText) {
			if (userMessage.empty() || assistantText.empty()) {
				return false;
			}

			const std::string normalizedUser =
				NormalizeForEchoCheck(userMessage);
			const std::string normalizedAssistant =
				NormalizeForEchoCheck(assistantText);
			if (normalizedUser.empty() || normalizedAssistant.empty()) {
				return false;
			}

			if (normalizedAssistant == normalizedUser) {
				return true;
			}

			if (normalizedAssistant.size() > normalizedUser.size() &&
				normalizedAssistant.rfind(normalizedUser, 0) == 0) {
				const std::string trailing = TrimAsciiWhitespace(
					normalizedAssistant.substr(normalizedUser.size()));
				return trailing.empty();
			}

			return false;
		}

		std::string WideToNarrowAscii(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		std::string BuildSkillsInjectedMessage(
			const std::string& userMessage,
			const std::wstring& skillsPrompt) {
			if (skillsPrompt.empty()) {
				return userMessage;
			}

			std::string narrowedPrompt = WideToNarrowAscii(skillsPrompt);
			if (narrowedPrompt.empty()) {
				return userMessage;
			}

			constexpr std::size_t kMaxPromptChars = 4000;
			if (narrowedPrompt.size() > kMaxPromptChars) {
				narrowedPrompt.resize(kMaxPromptChars);
			}

			std::string injected;
			injected.reserve(userMessage.size() + narrowedPrompt.size() + 64);
			injected += "[skills_prompt]\n";
			injected += narrowedPrompt;
			injected += "\n\n[user_message]\n";
			injected += userMessage;
			return injected;
		}

		bool IsOneOfChannels(
			const std::vector<std::wstring>& enabledChannels,
			const std::wstring& candidate) {
			for (const auto& channel : enabledChannels) {
				if (_wcsicmp(channel.c_str(), candidate.c_str()) == 0) {
					return true;
				}
			}

			return false;
		}

		bool ResolveHooksEngineEnabled(const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_ENGINE_ENABLED",
				config.hooks.engine.enabled);
		}

		bool ResolveHooksFallbackPromptInjection(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_FALLBACK_PROMPT_INJECTION",
				config.hooks.engine.fallbackPromptInjection);
		}

		bool ResolveHooksReminderEnabled(const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_REMINDER_ENABLED",
				config.hooks.engine.reminderEnabled);
		}

		std::wstring ResolveHooksReminderVerbosity(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.reminderVerbosity;
			wchar_t* env = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&env, &length, L"BLAZECLAW_HOOKS_REMINDER_VERBOSITY") == 0 &&
				env != nullptr &&
				length > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			std::wstring normalized;
			normalized.reserve(value.size());
			for (const wchar_t ch : value) {
				normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
			}

			if (normalized == L"minimal" || normalized == L"normal" || normalized == L"detailed") {
				return normalized;
			}

			return L"normal";
		}

		bool ResolveHooksGovernanceReportingEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZECLAW_HOOKS_GOVERNANCE_REPORTING_ENABLED",
				config.hooks.engine.governanceReportingEnabled);
		}

		std::filesystem::path ResolveHooksGovernanceReportDir(
			const blazeclaw::config::AppConfig& config) {
			std::wstring value = config.hooks.engine.governanceReportDir;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, L"BLAZECLAW_HOOKS_GOVERNANCE_REPORT_DIR") == 0 &&
				env != nullptr && len > 0) {
				value.assign(env);
			}
			if (env != nullptr) {
				free(env);
			}

			const auto trimmed = Trim(value);
			if (trimmed.empty()) {
				return std::filesystem::path(L"blazeclaw/reports/hooks-governance");
			}

			return std::filesystem::path(trimmed);
		}

		std::wstring BuildGovernanceReportJson(
			const HookExecutionSnapshot& execution,
			const std::vector<std::wstring>& allowedPackages,
			const bool strictMode) {
			std::wstringstream builder;
			builder << L"{\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\",\"strictPolicyEnforcement\":"
				<< (strictMode ? L"true" : L"false")
				<< L",\"allowedPackages\":[";

			for (std::size_t i = 0; i < allowedPackages.size(); ++i) {
				if (i > 0) {
					builder << L",";
				}
				builder << L"\"" << allowedPackages[i] << L"\"";
			}

			builder << L"],\"reminderState\":\""
				<< execution.diagnostics.lastReminderState
				<< L"\",\"reminderReason\":\""
				<< execution.diagnostics.lastReminderReason
				<< L"\"}";
			return builder.str();
		}

		bool WriteGovernanceReportFile(
			const std::filesystem::path& reportFile,
			const std::wstring& content) {
			std::ofstream output(reportFile, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				return false;
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content) {
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			return output.good();
		}

		void EmitGovernanceReportIfNeeded(
			const HookExecutionSnapshot& execution,
			const bool enabled,
			const std::filesystem::path& reportDir,
			const std::vector<std::wstring>& allowedPackages,
			const bool strictMode,
			std::uint64_t& inOutReportCount,
			std::wstring& outLastReportPath,
			std::vector<std::wstring>& inOutWarnings) {
			if (!enabled) {
				return;
			}

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0) {
				return;
			}

			std::error_code ec;
			auto fullDir = reportDir;
			if (fullDir.is_relative()) {
				fullDir = std::filesystem::current_path(ec) / fullDir;
			}

			std::filesystem::create_directories(fullDir, ec);
			if (ec) {
				inOutWarnings.push_back(
					L"hooks-governance report generation failed: cannot create report directory.");
				return;
			}

			const auto nonce = std::to_wstring(
				static_cast<std::uint64_t>(
					std::chrono::system_clock::now().time_since_epoch().count()));
			const auto reportFile = fullDir / (L"hooks-governance-" + nonce + L".json");
			const auto reportContent = BuildGovernanceReportJson(
				execution,
				allowedPackages,
				strictMode);
			if (!WriteGovernanceReportFile(reportFile, reportContent)) {
				inOutWarnings.push_back(
					L"hooks-governance report generation failed: cannot write report file.");
				return;
			}

			++inOutReportCount;
			outLastReportPath = reportFile.wstring();
		}

		bool ContainsBootstrapFile(
			const std::vector<HookBootstrapFile>& files,
			const std::wstring& expectedPath) {
			for (const auto& file : files) {
				std::wstring lowered = file.path;
				std::transform(
					lowered.begin(),
					lowered.end(),
					lowered.begin(),
					[](const wchar_t ch) {
						return static_cast<wchar_t>(std::towlower(ch));
					});

				std::wstring expected = expectedPath;
				std::transform(
					expected.begin(),
					expected.end(),
					expected.begin(),
					[](const wchar_t ch) {
						return static_cast<wchar_t>(std::towlower(ch));
					});

				if (lowered == expected) {
					return true;
				}
			}

			return false;
		}

		std::wstring BuildSelfEvolvingReminderPromptBlock() {
			return L"\n## Self-Evolving Reminder\n"
				L"When tasks finish, capture reusable learnings:\n"
				L"- corrections -> .learnings/LEARNINGS.md\n"
				L"- failures -> .learnings/ERRORS.md\n"
				L"- missing capabilities -> .learnings/FEATURE_REQUESTS.md\n"
				L"Promote proven patterns to AGENTS.md / SOUL.md / TOOLS.md.\n";
		}

		std::wstring BuildGenericHookBootstrapContextBlock(
			const std::vector<HookBootstrapFile>& bootstrapFiles) {
			std::wstringstream builder;
			bool headerWritten = false;
			for (const auto& file : bootstrapFiles) {
				const auto lowered = file.path;
				std::wstring normalized;
				normalized.reserve(lowered.size());
				for (const auto ch : lowered) {
					normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
				}

				if (normalized == L"self_evolving_reminder.md") {
					continue;
				}

				if (!headerWritten) {
					builder << L"\n## Hook Bootstrap Context\n";
					headerWritten = true;
				}

				builder << L"- " << file.path;
				if (file.virtualFile) {
					builder << L" (virtual)";
				}
				builder << L"\n";
			}

			return builder.str();
		}

		std::wstring BuildReminderSkipReason(const HookExecutionSnapshot& execution) {
			if (!execution.diagnostics.lastReminderReason.empty()) {
				return execution.diagnostics.lastReminderReason;
			}

			return L"none";
		}

	} // namespace

	ServiceManager::ServiceManager() = default;

	blazeclaw::gateway::SkillsCatalogGatewayState ServiceManager::BuildGatewaySkillsState() const {
		blazeclaw::gateway::SkillsCatalogGatewayState gatewaySkillsState;
		gatewaySkillsState.entries.reserve(m_skillsCatalog.entries.size());

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& eligibility : m_skillsEligibility.entries) {
			eligibilityByName.emplace(eligibility.skillName, eligibility);
		}

		std::unordered_map<std::wstring, SkillsCommandSpec> commandsBySkill;
		for (const auto& command : m_skillsCommands.commands) {
			commandsBySkill.emplace(command.skillName, command);
		}

		std::unordered_map<std::wstring, SkillsInstallPlanEntry> installBySkill;
		for (const auto& plan : m_skillsInstall.entries) {
			installBySkill.emplace(plan.skillName, plan);
		}

		for (const auto& entry : m_skillsCatalog.entries) {
			const auto eligibilityIt = eligibilityByName.find(entry.skillName);
			const bool hasEligibility = eligibilityIt != eligibilityByName.end();

			std::string commandName;
			const auto commandIt = commandsBySkill.find(entry.skillName);
			if (commandIt != commandsBySkill.end()) {
				commandName = ToNarrow(commandIt->second.name);
			}

			std::string installKind;
			std::string installCommand;
			std::string installReason;
			bool installExecutable = false;
			const auto installIt = installBySkill.find(entry.skillName);
			if (installIt != installBySkill.end()) {
				installKind = ToNarrow(installIt->second.kind);
				installCommand = ToNarrow(installIt->second.command);
				installReason = ToNarrow(installIt->second.reason);
				installExecutable = installIt->second.executable;
			}

			gatewaySkillsState.entries.push_back(
				blazeclaw::gateway::SkillsCatalogGatewayEntry{
					.name = ToNarrow(entry.skillName),
					.skillKey = hasEligibility ? ToNarrow(eligibilityIt->second.skillKey)
											   : ToNarrow(entry.skillName),
					.commandName = commandName,
					.installKind = installKind,
					.installCommand = installCommand,
					.installExecutable = installExecutable,
					.installReason = installReason,
					.description = ToNarrow(entry.description),
					.source = ToNarrow(SkillsCatalogService::SourceKindLabel(entry.sourceKind)),
					.precedence = entry.precedence,
					.eligible = hasEligibility ? eligibilityIt->second.eligible : false,
					.disabled = hasEligibility ? eligibilityIt->second.disabled : false,
					.blockedByAllowlist =
						hasEligibility ? eligibilityIt->second.blockedByAllowlist : false,
					.disableModelInvocation =
						hasEligibility ? eligibilityIt->second.disableModelInvocation : false,
					.validFrontmatter = entry.validFrontmatter,
					.validationErrorCount = entry.validationErrors.size(),
				});
		}

		gatewaySkillsState.rootsScanned = m_skillsCatalog.diagnostics.rootsScanned;
		gatewaySkillsState.rootsSkipped = m_skillsCatalog.diagnostics.rootsSkipped;
		gatewaySkillsState.oversizedSkillFiles =
			m_skillsCatalog.diagnostics.oversizedSkillFiles;
		gatewaySkillsState.invalidFrontmatterFiles =
			m_skillsCatalog.diagnostics.invalidFrontmatterFiles;
		gatewaySkillsState.warningCount = m_skillsCatalog.diagnostics.warnings.size();
		gatewaySkillsState.eligibleCount = m_skillsEligibility.eligibleCount;
		gatewaySkillsState.disabledCount = m_skillsEligibility.disabledCount;
		gatewaySkillsState.blockedByAllowlistCount =
			m_skillsEligibility.blockedByAllowlistCount;
		gatewaySkillsState.missingRequirementsCount =
			m_skillsEligibility.missingRequirementsCount;
		gatewaySkillsState.promptIncludedCount = m_skillsPrompt.includedCount;
		gatewaySkillsState.promptChars = m_skillsPrompt.promptChars;
		gatewaySkillsState.promptTruncated = m_skillsPrompt.truncated;
		gatewaySkillsState.snapshotVersion = m_skillsWatch.version;
		gatewaySkillsState.watchEnabled = m_skillsWatch.watchEnabled;
		gatewaySkillsState.watchDebounceMs = m_skillsWatch.debounceMs;
		gatewaySkillsState.watchReason = ToNarrow(m_skillsWatch.reason);
		gatewaySkillsState.prompt = ToNarrow(m_skillsPrompt.prompt);
		gatewaySkillsState.sandboxSyncOk = m_skillsSync.success;
		gatewaySkillsState.sandboxSynced = m_skillsSync.copiedSkills;
		gatewaySkillsState.sandboxSkipped = m_skillsSync.skippedSkills;
		gatewaySkillsState.envAllowed = m_skillsEnvOverrides.allowedCount;
		gatewaySkillsState.envBlocked = m_skillsEnvOverrides.blockedCount;
		gatewaySkillsState.installExecutableCount = m_skillsInstall.executableCount;
		gatewaySkillsState.installBlockedCount = m_skillsInstall.blockedCount;
		gatewaySkillsState.scanInfoCount = m_skillSecurityScan.infoCount;
		gatewaySkillsState.scanWarnCount = m_skillSecurityScan.warnCount;
		gatewaySkillsState.scanCriticalCount = m_skillSecurityScan.criticalCount;
		gatewaySkillsState.scanScannedFiles = m_skillSecurityScan.scannedFileCount;
		gatewaySkillsState.governanceReportingEnabled = m_hooksGovernanceReportingEnabled;
		gatewaySkillsState.governanceReportsGenerated =
			static_cast<std::size_t>(m_hooksGovernanceReportsGenerated);
		gatewaySkillsState.lastGovernanceReportPath =
			ToNarrow(m_hooksLastGovernanceReportPath);
		gatewaySkillsState.policyBlockedCount =
			static_cast<std::size_t>(m_hookExecution.diagnostics.policyBlockedCount);
		gatewaySkillsState.driftDetectedCount =
			static_cast<std::size_t>(m_hookExecution.diagnostics.driftDetectedCount);
		gatewaySkillsState.lastDriftReason =
			ToNarrow(m_hookExecution.diagnostics.lastDriftReason);
		gatewaySkillsState.autoRemediationEnabled = m_hooksAutoRemediationEnabled;
		gatewaySkillsState.autoRemediationRequiresApproval =
			m_hooksAutoRemediationRequiresApproval;
		gatewaySkillsState.autoRemediationExecuted =
			static_cast<std::size_t>(m_hooksAutoRemediationExecuted);
		gatewaySkillsState.lastAutoRemediationStatus =
			ToNarrow(m_hooksLastAutoRemediationStatus);
		gatewaySkillsState.autoRemediationTenantId =
			ToNarrow(m_hooksAutoRemediationTenantId);
		gatewaySkillsState.lastAutoRemediationPlaybookPath =
			ToNarrow(m_hooksLastAutoRemediationPlaybookPath);
		gatewaySkillsState.autoRemediationTokenMaxAgeMinutes =
			static_cast<std::size_t>(m_hooksAutoRemediationTokenMaxAgeMinutes);
		gatewaySkillsState.autoRemediationTokenRotations =
			static_cast<std::size_t>(m_hooksAutoRemediationTokenRotations);
		gatewaySkillsState.lastRemediationTelemetryPath =
			ToNarrow(m_hooksLastRemediationTelemetryPath);
		gatewaySkillsState.lastRemediationAuditPath =
			ToNarrow(m_hooksLastRemediationAuditPath);
		gatewaySkillsState.remediationSloStatus =
			ToNarrow(m_hooksRemediationSloStatus);
		gatewaySkillsState.remediationSloMaxDriftDetected =
			static_cast<std::size_t>(m_hooksRemediationSloMaxDriftDetected);
		gatewaySkillsState.remediationSloMaxPolicyBlocked =
			static_cast<std::size_t>(m_hooksRemediationSloMaxPolicyBlocked);
		gatewaySkillsState.lastComplianceAttestationPath =
			ToNarrow(m_hooksLastComplianceAttestationPath);
		gatewaySkillsState.enterpriseSlaPolicyId =
			ToNarrow(m_hooksEnterpriseSlaPolicyId);
		gatewaySkillsState.crossTenantAttestationAggregationEnabled =
			m_hooksCrossTenantAttestationAggregationEnabled;
		gatewaySkillsState.crossTenantAttestationAggregationStatus =
			ToNarrow(m_hooksCrossTenantAttestationAggregationStatus);
		gatewaySkillsState.crossTenantAttestationAggregationCount =
			static_cast<std::size_t>(m_hooksCrossTenantAttestationAggregationCount);
		gatewaySkillsState.lastCrossTenantAttestationAggregationPath =
			ToNarrow(m_hooksLastCrossTenantAttestationAggregationPath);
		return gatewaySkillsState;
	}

	void ServiceManager::RefreshSkillsState(
		const blazeclaw::config::AppConfig& config,
		const bool forceRefresh,
		const std::wstring& reason) {
		m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
			std::filesystem::current_path(),
			config);
		m_skillsEligibility = m_skillsEligibilityService.Evaluate(
			m_skillsCatalog,
			config);
		m_hookCatalog = m_hookCatalogService.BuildSnapshot(m_skillsCatalog);
		m_hookExecution = m_hookExecutionService.Snapshot();
		m_skillsPrompt = m_skillsPromptService.BuildSnapshot(
			m_skillsCatalog,
			m_skillsEligibility,
			config,
			std::nullopt,
			m_hooksFallbackPromptInjection);
		m_hookEvents = m_hookEventService.Snapshot();
		m_skillsCommands = m_skillsCommandService.BuildSnapshot(
			m_skillsCatalog,
			m_skillsEligibility);
		m_skillsSync = m_skillsSyncService.SyncToSandbox(
			std::filesystem::current_path(),
			m_skillsCatalog,
			m_skillsEligibility,
			config);
		m_skillsEnvOverrides = m_skillsEnvOverrideService.BuildSnapshot(
			m_skillsCatalog,
			m_skillsEligibility,
			config);
		m_skillsInstall = m_skillsInstallService.BuildSnapshot(
			m_skillsCatalog,
			m_skillsEligibility,
			config);
		m_skillSecurityScan = m_skillSecurityScanService.BuildSnapshot(
			m_skillsCatalog,
			m_skillsEligibility,
			config);
		m_skillsEnvOverrideService.Apply(m_skillsEnvOverrides);
		m_skillsWatch = m_skillsWatchService.Observe(
			m_skillsCatalog,
			config,
			forceRefresh,
			reason);
	}

	bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
		if (m_running) {
			return true;
		}

		m_activeConfig = config;
		m_activeChatProvider = config.chat.activeProvider.empty()
			? "local"
			: ToNarrow(config.chat.activeProvider);
		m_activeChatModel = config.chat.activeModel.empty()
			? "default"
			: ToNarrow(config.chat.activeModel);
		m_hooksEngineEnabled = ResolveHooksEngineEnabled(m_activeConfig);
		m_hooksFallbackPromptInjection =
			ResolveHooksFallbackPromptInjection(m_activeConfig);
		m_hooksReminderEnabled = ResolveHooksReminderEnabled(m_activeConfig);
		m_hooksReminderVerbosity = ResolveHooksReminderVerbosity(m_activeConfig);
		m_hooksAllowedPackages = ResolveHooksAllowedPackages(m_activeConfig);
		m_hooksStrictPolicyEnforcement =
			ResolveHooksStrictPolicyEnforcement(m_activeConfig);
		m_hooksGovernanceReportingEnabled =
			ResolveHooksGovernanceReportingEnabled(m_activeConfig);
		m_hooksGovernanceReportDir =
			ResolveHooksGovernanceReportDir(m_activeConfig);
		m_hooksAutoRemediationEnabled =
			ResolveHooksAutoRemediationEnabled(m_activeConfig);
		m_hooksAutoRemediationRequiresApproval =
			ResolveHooksAutoRemediationRequiresApproval(m_activeConfig);
		m_hooksAutoRemediationApprovalToken =
			ResolveHooksAutoRemediationApprovalToken(m_activeConfig);
		m_hooksAutoRemediationTenantId =
			ResolveHooksAutoRemediationTenantId(m_activeConfig);
		m_hooksAutoRemediationPlaybookDir =
			ResolveHooksAutoRemediationPlaybookDir(m_activeConfig);
		m_hooksAutoRemediationTokenMaxAgeMinutes =
			ResolveHooksAutoRemediationTokenMaxAgeMinutes(m_activeConfig);
		m_hooksRemediationTelemetryEnabled =
			ResolveHooksRemediationTelemetryEnabled(m_activeConfig);
		m_hooksRemediationTelemetryDir =
			ResolveHooksRemediationTelemetryDir(m_activeConfig);
		m_hooksRemediationAuditEnabled =
			ResolveHooksRemediationAuditEnabled(m_activeConfig);
		m_hooksRemediationAuditDir =
			ResolveHooksRemediationAuditDir(m_activeConfig);
		m_hooksRemediationSloMaxDriftDetected =
			ResolveHooksRemediationSloMaxDriftDetected(m_activeConfig);
		m_hooksRemediationSloMaxPolicyBlocked =
			ResolveHooksRemediationSloMaxPolicyBlocked(m_activeConfig);
		m_hooksComplianceAttestationEnabled =
			ResolveHooksComplianceAttestationEnabled(m_activeConfig);
		m_hooksComplianceAttestationDir =
			ResolveHooksComplianceAttestationDir(m_activeConfig);
		m_hooksEnterpriseSlaGovernanceEnabled =
			ResolveHooksEnterpriseSlaGovernanceEnabled(m_activeConfig);
		m_hooksEnterpriseSlaPolicyId =
			ResolveHooksEnterpriseSlaPolicyId(m_activeConfig);
		m_hooksCrossTenantAttestationAggregationEnabled =
			ResolveHooksCrossTenantAttestationAggregationEnabled(m_activeConfig);
		m_hooksCrossTenantAttestationAggregationDir =
			ResolveHooksCrossTenantAttestationAggregationDir(m_activeConfig);
		m_hooksGovernanceReportsGenerated = 0;
		m_hooksLastGovernanceReportPath.clear();
		m_hooksAutoRemediationExecuted = 0;
		m_hooksLastAutoRemediationStatus = L"idle";
		m_hooksLastAutoRemediationPlaybookPath.clear();
		m_hooksAutoRemediationTokenRotations = 0;
		m_hooksLastRemediationTelemetryPath.clear();
		m_hooksLastRemediationAuditPath.clear();
		m_hooksRemediationSloStatus = L"unknown";
		m_hooksLastComplianceAttestationPath.clear();
		m_hooksLastCrossTenantAttestationAggregationPath.clear();
		m_hooksCrossTenantAttestationAggregationCount = 0;
		m_hooksCrossTenantAttestationAggregationStatus = L"idle";
		m_selfEvolvingHookTriggered = false;
		m_agentsScope = m_agentsCatalogService.BuildSnapshot(
			std::filesystem::current_path(),
			m_activeConfig);
		m_agentsWorkspace = m_agentsWorkspaceService.BuildSnapshot(m_agentsScope);
		m_agentsToolPolicy = m_agentsToolPolicyService.BuildSnapshot(
			m_agentsScope,
			m_activeConfig);
		m_agentsShellRuntimeService.Configure(m_activeConfig);
		m_agentsModelRoutingService.Configure(m_activeConfig);
		m_modelRouting = m_agentsModelRoutingService.Snapshot();
		m_agentsAuthProfileService.Configure(m_activeConfig);
		m_agentsAuthProfileService.Initialize(std::filesystem::current_path());
		m_authProfiles = m_agentsAuthProfileService.Snapshot(1735690000000);
		m_sandbox = m_agentsSandboxService.BuildSnapshot(m_agentsScope, m_activeConfig);
		m_agentsTranscriptSafetyService.Configure(m_activeConfig);
		m_subagentRegistryService.Configure(m_activeConfig);
		m_subagentRegistryService.Initialize(std::filesystem::current_path());
		m_subagentRegistry = m_subagentRegistryService.Snapshot();
		m_lastAcpDecision = m_acpSpawnService.Evaluate(
			m_activeConfig,
			AcpSpawnRequest{
				.requesterSessionId = "main",
				.requesterAgentId = "default",
				.targetAgentId = "",
				.threadRequested = false,
				.requesterSandboxed = false,
			});
		m_embeddingsService.Configure(m_activeConfig);
		m_embeddings = m_embeddingsService.Snapshot();

		m_localModelRolloutEligible = IsLocalModelRolloutEligible();
		m_localModelActivationEnabled = false;
		m_localModelActivationReason.clear();

		if (!m_activeConfig.localModel.enabled) {
			m_localModelActivationReason = "config_disabled";
		}
		else if (!m_localModelRolloutEligible) {
			m_localModelActivationReason = "rollout_stage_not_eligible";
		}

		m_localModelRuntime.Configure(m_activeConfig);
		const bool localModelLoaded = m_localModelRuntime.LoadModel();
		m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
		if (!localModelLoaded && m_localModelRuntimeSnapshot.status.empty()) {
			m_localModelRuntimeSnapshot.status = "load_failed";
		}

		if (m_activeConfig.localModel.enabled &&
			m_localModelRolloutEligible &&
			localModelLoaded &&
			m_localModelRuntimeSnapshot.ready) {
			m_localModelActivationEnabled = true;
			m_localModelActivationReason = "active";
		}
		else if (m_activeConfig.localModel.enabled &&
			m_localModelRolloutEligible &&
			!m_localModelRuntimeSnapshot.ready) {
			m_localModelActivationReason = "initialization_failed";
		}

		TRACE(
			"[LocalModel] startup.gating enabled=%s rolloutEligible=%s activation=%s reason=%s stage=%S status=%s\n",
			m_activeConfig.localModel.enabled ? "true" : "false",
			m_localModelRolloutEligible ? "true" : "false",
			m_localModelActivationEnabled ? "true" : "false",
			m_localModelActivationReason.c_str(),
			m_activeConfig.localModel.rolloutStage.c_str(),
			m_localModelRuntimeSnapshot.status.c_str());

		if (m_localModelActivationEnabled && m_localModelRuntimeSnapshot.ready) {
			std::string localContractFailure;
			if (!m_localModelRuntime.VerifyDeterministicContract(localContractFailure)) {
				m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
				const bool enforceContract =
					_wcsicmp(m_activeConfig.localModel.rolloutStage.c_str(), L"dev") != 0;
				m_localModelRuntimeSnapshot.status = enforceContract
					? "contract_verification_failed"
					: "contract_verification_warning";
				m_localModelActivationEnabled = !enforceContract;
				m_localModelActivationReason = enforceContract
					? "contract_verification_failed"
					: "active_contract_warning";
				if (!localContractFailure.empty()) {
					m_localModelRuntimeSnapshot.error = localmodel::TextGenerationError{
						.code = localmodel::TextGenerationErrorCode::InferenceFailed,
						.message = localContractFailure,
					};
				}

				m_skillsCatalog.diagnostics.warnings.push_back(
					L"local-model deterministic contract verification failed");
			}
		}

		m_retrievalMemoryService.Configure(m_activeConfig);
		m_retrievalMemory = m_retrievalMemoryService.Snapshot();
		m_piEmbeddedService.Configure(m_activeConfig);
		RefreshSkillsState(m_activeConfig, true, L"startup");

		std::wstring hookEventError;
		const bool emittedBootstrapEvent = m_hookEventService.EmitAgentBootstrap(
			L"main",
			std::vector<HookBootstrapFile>{
			HookBootstrapFile{ .path = L"BOOTSTRAP.md", .virtualFile = true },
				HookBootstrapFile{ .path = L"MEMORY.md", .virtualFile = true }},
			hookEventError);
		m_hookEvents = m_hookEventService.Snapshot();
		if (!emittedBootstrapEvent && !hookEventError.empty()) {
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"hooks-event emission failed: " + hookEventError);
		}

		if (m_hooksEngineEnabled && emittedBootstrapEvent && !m_hookEvents.events.empty()) {
			std::wstring dispatchError;
			const auto& latestEvent = m_hookEvents.events.back();
			HookLifecycleEvent eventForDispatch = latestEvent;
			if (m_hooksReminderVerbosity == L"detailed") {
				eventForDispatch.bootstrapFiles.push_back(
					HookBootstrapFile{ .path = L"HOOK_RUNTIME_DETAILED_CONTEXT.md", .virtualFile = true });
			}
			if (!m_hookExecutionService.Dispatch(
				eventForDispatch,
				m_hookCatalog,
				HookExecutionPolicy{
					.reminderEnabled = m_hooksReminderEnabled,
					.reminderVerbosity = m_hooksReminderVerbosity,
					.allowedPackages = m_hooksAllowedPackages,
					.strictPolicyEnforcement = m_hooksStrictPolicyEnforcement },
					dispatchError) &&
					!dispatchError.empty()) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"hooks-dispatch failed: " + dispatchError);
			}

			m_hookExecution = m_hookExecutionService.Snapshot();
			EmitGovernanceReportIfNeeded(
				m_hookExecution,
				m_hooksGovernanceReportingEnabled,
				m_hooksGovernanceReportDir,
				m_hooksAllowedPackages,
				m_hooksStrictPolicyEnforcement,
				m_hooksGovernanceReportsGenerated,
				m_hooksLastGovernanceReportPath,
				m_skillsCatalog.diagnostics.warnings);
			EmitTenantRemediationPlaybookIfNeeded(
				m_hookExecution,
				m_hooksAutoRemediationEnabled,
				m_hooksAutoRemediationTenantId,
				m_hooksAutoRemediationPlaybookDir,
				m_hooksLastGovernanceReportPath,
				m_hooksAutoRemediationTokenMaxAgeMinutes,
				m_hooksLastAutoRemediationPlaybookPath,
				m_skillsCatalog.diagnostics.warnings);
			if (!m_hooksLastAutoRemediationPlaybookPath.empty()) {
				m_hooksLastAutoRemediationStatus = L"playbook_generated";
			}
			EmitRemediationTelemetryAndAuditIfNeeded(
				m_hookExecution,
				m_hooksRemediationTelemetryEnabled,
				m_hooksRemediationTelemetryDir,
				m_hooksRemediationAuditEnabled,
				m_hooksRemediationAuditDir,
				m_hooksAutoRemediationTenantId,
				m_hooksAutoRemediationApprovalToken,
				m_hooksLastGovernanceReportPath,
				m_hooksLastAutoRemediationPlaybookPath,
				m_hooksLastAutoRemediationStatus,
				m_hooksAutoRemediationTokenRotations,
				m_hooksLastRemediationTelemetryPath,
				m_hooksLastRemediationAuditPath,
				m_skillsCatalog.diagnostics.warnings);
			m_hooksRemediationSloStatus = EvaluateRemediationSloStatus(
				m_hookExecution,
				m_hooksRemediationSloMaxDriftDetected,
				m_hooksRemediationSloMaxPolicyBlocked);
			EmitComplianceAttestationIfNeeded(
				m_hooksComplianceAttestationEnabled,
				m_hooksComplianceAttestationDir,
				m_hooksAutoRemediationTenantId,
				m_hooksRemediationSloStatus,
				m_hookExecution,
				m_hooksLastRemediationTelemetryPath,
				m_hooksLastRemediationAuditPath,
				m_hooksLastComplianceAttestationPath,
				m_skillsCatalog.diagnostics.warnings);
			EmitCrossTenantAttestationAggregationIfNeeded(
				m_hooksCrossTenantAttestationAggregationEnabled,
				m_hooksCrossTenantAttestationAggregationDir,
				m_hooksAutoRemediationTenantId,
				m_hooksEnterpriseSlaPolicyId,
				m_hooksRemediationSloStatus,
				m_hooksLastComplianceAttestationPath,
				m_hookExecution,
				m_hooksCrossTenantAttestationAggregationCount,
				m_hooksCrossTenantAttestationAggregationStatus,
				m_hooksLastCrossTenantAttestationAggregationPath,
				m_skillsCatalog.diagnostics.warnings);
			m_selfEvolvingHookTriggered = ContainsBootstrapFile(
				m_hookExecution.bootstrapFiles,
				L"SELF_EVOLVING_REMINDER.md");
			if (m_selfEvolvingHookTriggered &&
				m_skillsPrompt.prompt.find(L"## Self-Evolving Reminder") == std::wstring::npos) {
				m_skillsPrompt.prompt += BuildSelfEvolvingReminderPromptBlock();
				m_skillsPrompt.promptChars =
					static_cast<std::uint32_t>(m_skillsPrompt.prompt.size());
				if (m_skillsPrompt.prompt.size() >
					m_activeConfig.skills.limits.maxSkillsPromptChars) {
					m_skillsPrompt.prompt = m_skillsPrompt.prompt.substr(
						0,
						m_activeConfig.skills.limits.maxSkillsPromptChars);
					m_skillsPrompt.promptChars =
						static_cast<std::uint32_t>(m_skillsPrompt.prompt.size());
					m_skillsPrompt.truncated = true;
				}

				m_hookExecution.diagnostics.lastReminderState = L"reminder_fallback_used";
				m_hookExecution.diagnostics.lastReminderReason = L"prompt_fallback";
			}

			const std::wstring genericHookContext = BuildGenericHookBootstrapContextBlock(
				m_hookExecution.bootstrapFiles);
			if (!genericHookContext.empty() &&
				m_skillsPrompt.prompt.find(L"## Hook Bootstrap Context") == std::wstring::npos) {
				m_skillsPrompt.prompt += genericHookContext;
				m_skillsPrompt.promptChars =
					static_cast<std::uint32_t>(m_skillsPrompt.prompt.size());
				if (m_skillsPrompt.prompt.size() >
					m_activeConfig.skills.limits.maxSkillsPromptChars) {
					m_skillsPrompt.prompt = m_skillsPrompt.prompt.substr(
						0,
						m_activeConfig.skills.limits.maxSkillsPromptChars);
					m_skillsPrompt.promptChars =
						static_cast<std::uint32_t>(m_skillsPrompt.prompt.size());
					m_skillsPrompt.truncated = true;
				}
			}
		}
		else if (!m_hooksEngineEnabled) {
			++m_hookExecution.diagnostics.skippedCount;
			m_hookExecution.diagnostics.lastReminderState = L"reminder_skipped";
			m_hookExecution.diagnostics.lastReminderReason = L"hook_engine_disabled";
		}

		std::wstring fixtureError;
		const std::vector<std::filesystem::path> fixtureCandidates = {
			std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
			std::filesystem::current_path() / L"fixtures" / L"agents",
			std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
			std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
		};

		for (const auto& candidate : fixtureCandidates) {
			std::error_code ec;
			if (!std::filesystem::is_directory(candidate, ec) || ec) {
				continue;
			}

			const std::wstring candidateName = candidate.filename().wstring();
			if (candidateName == L"agents") {
				if (!m_agentsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-scope fixture validation failed: " + fixtureError);
				}

				if (!m_agentsWorkspaceService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-workspace fixture validation failed: " + fixtureError);
				}

				if (!m_agentsToolPolicyService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-tool-policy fixture validation failed: " + fixtureError);
				}

				if (!m_agentsShellRuntimeService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-shell-runtime fixture validation failed: " + fixtureError);
				}

				if (!m_agentsModelRoutingService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-model-routing fixture validation failed: " + fixtureError);
				}

				if (!m_agentsAuthProfileService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-auth-profile fixture validation failed: " + fixtureError);
				}

				if (!m_agentsSandboxService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-sandbox fixture validation failed: " + fixtureError);
				}

				if (!m_agentsTranscriptSafetyService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-transcript fixture validation failed: " + fixtureError);
				}

				if (!m_subagentRegistryService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-subagent fixture validation failed: " + fixtureError);
				}

				if (!m_acpSpawnService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-acp fixture validation failed: " + fixtureError);
				}

				if (!m_embeddingsService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-embeddings fixture validation failed: " + fixtureError);
				}

				if (!m_retrievalMemoryService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-retrieval fixture validation failed: " + fixtureError);
				}

				if (!m_piEmbeddedService.ValidateFixtureScenarios(candidate, fixtureError)) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"agents-embedded fixture validation failed: " + fixtureError);
				}

				continue;
			}

			if (!m_skillsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-catalog fixture validation failed: " + fixtureError);
			}

			if (!m_skillsEligibilityService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-eligibility fixture validation failed: " + fixtureError);
			}

			if (!m_skillsPromptService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-prompt fixture validation failed: " + fixtureError);
			}

			if (!m_skillsCommandService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-command fixture validation failed: " + fixtureError);
			}

			if (!m_skillsWatchService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-watch fixture validation failed: " + fixtureError);
			}

			if (!m_skillsSyncService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-sync fixture validation failed: " + fixtureError);
			}

			if (!m_skillsEnvOverrideService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-env fixture validation failed: " + fixtureError);
			}

			if (!m_skillsInstallService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-install fixture validation failed: " + fixtureError);
			}

			if (!m_skillSecurityScanService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"skills-scan fixture validation failed: " + fixtureError);
			}

			if (!m_hookCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"hooks-catalog fixture validation failed: " + fixtureError);
			}

			if (!m_hookEventService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"hooks-events fixture validation failed: " + fixtureError);
			}

			if (!m_hookExecutionService.ValidateFixtureScenarios(candidate, fixtureError)) {
				m_skillsCatalog.diagnostics.warnings.push_back(
					L"hooks-execution fixture validation failed: " + fixtureError);
			}

			break;
		}

		m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
		m_gatewayHost.SetSkillsRefreshCallback([this]() {
			RefreshSkillsState(m_activeConfig, true, L"manual-refresh");
			return BuildGatewaySkillsState();
			});

		m_gatewayHost.SetChatRuntimeCallback([this](
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
				const std::string sessionId =
					request.sessionKey.empty() ? "main" : request.sessionKey;
				const std::string runtimeMessage =
					BuildSkillsInjectedMessage(request.message, m_skillsPrompt.prompt);

				std::vector<EmbeddedToolBinding> toolBindings;
				toolBindings.reserve(m_skillsCommands.commands.size());
				for (const auto& command : m_skillsCommands.commands) {
					if (!command.dispatch.enabled ||
						_wcsicmp(command.dispatch.kind.c_str(), L"tool") != 0 ||
						command.dispatch.toolName.empty()) {
						continue;
					}

					toolBindings.push_back(EmbeddedToolBinding{
						.commandName = WideToNarrowAscii(command.name),
						.description = WideToNarrowAscii(command.description),
						.toolName = WideToNarrowAscii(command.dispatch.toolName),
						});
				}

				const auto embeddedExecution = m_piEmbeddedService.ExecuteRun(
					EmbeddedRuntimeExecutionRequest{
						.run = EmbeddedRunRequest{
							.sessionId = sessionId,
							.agentId = "default",
							.message = request.message,
						},
						.skillsPrompt = WideToNarrowAscii(m_skillsPrompt.prompt),
						.toolBindings = std::move(toolBindings),
						.runtimeTools = m_gatewayHost.ListRuntimeTools(),
                     .enableDynamicToolLoop =
							m_activeConfig.embedded.dynamicToolLoopEnabled,
						.toolExecutor = [this](
											const std::string& tool,
											const std::optional<std::string>& argsJson) {
						  return m_gatewayHost.ExecuteRuntimeTool(tool, argsJson);
						},
					});

				if (!embeddedExecution.accepted) {
					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = m_activeChatModel,
						.errorCode = "embedded_run_rejected",
						.errorMessage = embeddedExecution.errorMessage.empty()
							? embeddedExecution.reason
							: embeddedExecution.errorMessage,
					};
				}

				if (embeddedExecution.handled) {
					if (!embeddedExecution.success) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.assistantDeltas = embeddedExecution.assistantDeltas,
							.modelId = m_activeChatModel,
							.errorCode = embeddedExecution.errorCode.empty()
								? "embedded_tool_execution_failed"
								: embeddedExecution.errorCode,
							.errorMessage = embeddedExecution.errorMessage.empty()
								? "embedded tool orchestration failed"
								: embeddedExecution.errorMessage,
						};
					}

					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = true,
						.assistantText = embeddedExecution.assistantText,
						.assistantDeltas = embeddedExecution.assistantDeltas,
						.modelId = m_activeChatModel,
						.errorCode = {},
						.errorMessage = {},
					};
				}

				auto providerRequest = request;
				providerRequest.message = runtimeMessage;

				if (m_activeChatProvider == "deepseek") {
					ClearDeepSeekRunCancelled(providerRequest.runId);
					if (!HasDeepSeekCredential()) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = m_activeChatModel,
							.errorCode = "deepseek_api_key_missing",
							.errorMessage =
								"DeepSeek API key missing. Configure DeepSeek extension first.",
						};
					}

					const std::string effectiveModel = NormalizeDeepSeekApiModelId(
						m_activeChatModel.empty() ? "deepseek-chat" : m_activeChatModel);
					const auto apiKey = ResolveDeepSeekCredentialUtf8();
					if (!apiKey.has_value() || apiKey->empty()) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = effectiveModel,
							.errorCode = "deepseek_api_key_missing",
							.errorMessage =
								"DeepSeek API key missing. Configure DeepSeek extension first.",
						};
					}

					return InvokeDeepSeekRemoteChat(providerRequest, effectiveModel, apiKey.value());
				}

				if (m_localModelActivationEnabled) {
					const std::string prompt = BuildLocalModelPrompt(providerRequest);
					TRACE(
						"[LocalModel] request.enqueue runId=%s session=%s promptChars=%zu attachments=%s\n",
						providerRequest.runId.c_str(),
						sessionId.c_str(),
						prompt.size(),
						providerRequest.hasAttachments ? "true" : "false");
					TRACE(
						"[LocalModel] request.start runId=%s\n",
						providerRequest.runId.c_str());

					const auto localResult = m_localModelRuntime.GenerateStream(
						localmodel::TextGenerationRequest{
							.runId = providerRequest.runId,
							.prompt = prompt,
							.maxTokens = std::nullopt,
							.temperature = std::nullopt,
						},
						nullptr);
					m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();

					if (!localResult.ok) {
						const std::string errorCode =
							localResult.error.has_value()
							? localmodel::TextGenerationErrorCodeToString(
								localResult.error->code)
							: "chat_runtime_error";
						const std::string errorMessage =
							localResult.error.has_value() &&
							!localResult.error->message.empty()
							? localResult.error->message
							: "local model generation failed";
						TRACE(
							"[LocalModel] request.terminal runId=%s state=%s latencyMs=%u tokens=%u reason=%s\n",
							providerRequest.runId.c_str(),
							localResult.cancelled ? "aborted" : "error",
							localResult.latencyMs,
							localResult.generatedTokens,
							errorMessage.c_str());
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = localResult.modelId,
							.errorCode = errorCode,
							.errorMessage = errorMessage,
						};
					}

					std::string assistantText = localResult.text;
					std::string modelId = localResult.modelId;
					std::uint32_t latencyMs = localResult.latencyMs;
					std::uint32_t generatedTokens = localResult.generatedTokens;
					if (IsLikelyEchoResponse(request.message, assistantText)) {
						TRACE(
							"[LocalModel] request.retry runId=%s reason=echo_detected\n",
							providerRequest.runId.c_str());
						const std::string retryPrompt = BuildLocalModelRetryPrompt(providerRequest);
						const auto retryResult = m_localModelRuntime.GenerateStream(
							localmodel::TextGenerationRequest{
								.runId = providerRequest.runId + "-retry",
								.prompt = retryPrompt,
								.maxTokens = std::nullopt,
								.temperature = std::nullopt,
							},
							nullptr);
						m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();

						if (retryResult.ok &&
							!IsLikelyEchoResponse(request.message, retryResult.text)) {
							assistantText = retryResult.text;
							modelId = retryResult.modelId;
							latencyMs = retryResult.latencyMs;
							generatedTokens = retryResult.generatedTokens;
						}
					}

					if (IsLikelyEchoResponse(request.message, assistantText)) {
						TRACE(
							"[LocalModel] request.terminal runId=%s state=error latencyMs=%u tokens=%u reason=echo_output_detected\n",
							providerRequest.runId.c_str(),
							latencyMs,
							generatedTokens);
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = modelId,
							.errorCode = "local_model_echo_output",
							.errorMessage = "local model echoed user input",
						};
					}

					TRACE(
						"[LocalModel] request.terminal runId=%s state=final latencyMs=%u tokens=%u\n",
						providerRequest.runId.c_str(),
						latencyMs,
						generatedTokens);

					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = true,
						.assistantText = assistantText,
						.modelId = modelId,
						.errorCode = {},
						.errorMessage = {},
					};
				}

				if (m_activeConfig.localModel.enabled &&
					!m_localModelActivationEnabled) {
					TRACE(
						"[LocalModel] request.fallback runId=%s reason=%s rolloutEligible=%s status=%s\n",
						request.runId.c_str(),
						m_localModelActivationReason.c_str(),
						m_localModelRolloutEligible ? "true" : "false",
						m_localModelRuntimeSnapshot.status.c_str());
				}

				const auto modelSelection = m_agentsModelRoutingService.SelectModel(
					m_activeConfig.agent.model.empty()
					? std::string()
					: ToNarrow(m_activeConfig.agent.model),
					"chat.send");

				std::string retrievalContext;
				if (m_activeConfig.embeddings.enabled && !request.message.empty()) {
					const auto userEmbedding = m_embeddingsService.EmbedText(
						EmbeddingRequest{
							.text = ToWide(request.message),
							.normalize = true,
							.traceId = "chat-retrieval-query",
						});
					if (userEmbedding.ok) {
						const auto matches = m_retrievalMemoryService.Query(
							sessionId,
							userEmbedding.vector,
							2);
						if (!matches.empty()) {
							retrievalContext = " [ctx:";
							for (std::size_t i = 0; i < matches.size(); ++i) {
								if (i > 0) {
									retrievalContext += " | ";
								}

								retrievalContext += matches[i].text;
							}

							retrievalContext += "]";
						}

						m_retrievalMemoryService.Upsert(
							sessionId,
							"user",
							request.message,
							userEmbedding.vector,
							CurrentEpochMs());
						m_retrievalMemory = m_retrievalMemoryService.Snapshot();
					}
				}

				const auto embeddedRun = m_piEmbeddedService.QueueRun(
					EmbeddedRunRequest{
						.sessionId = sessionId,
						.agentId = "default",
						.message = request.message,
					});

				if (!embeddedRun.accepted) {
					m_agentsModelRoutingService.RecordFailover(
						modelSelection.selectedModel,
						embeddedRun.reason,
						embeddedRun.startedAtMs == 0 ? 1735689800000 : embeddedRun.startedAtMs);
					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = modelSelection.selectedModel,
						.errorCode = "embedded_run_rejected",
						.errorMessage = embeddedRun.reason,
					};
				}

				const std::string assistantText = request.message.empty()
					? "Received image attachment."
					: ("Model(" + modelSelection.selectedModel + "): " +
						request.message + retrievalContext);

				if (m_activeConfig.embeddings.enabled && !assistantText.empty()) {
					const auto assistantEmbedding = m_embeddingsService.EmbedText(
						EmbeddingRequest{
							.text = ToWide(assistantText),
							.normalize = true,
							.traceId = "chat-retrieval-index",
						});
					if (assistantEmbedding.ok) {
						m_retrievalMemoryService.Upsert(
							sessionId,
							"assistant",
							assistantText,
							assistantEmbedding.vector,
							CurrentEpochMs());
						m_retrievalMemory = m_retrievalMemoryService.Snapshot();
					}
				}

				const bool completed = m_piEmbeddedService.CompleteRun(
					embeddedRun.runId,
					"completed",
					embeddedRun.startedAtMs + 1);
				if (!completed) {
					m_agentsModelRoutingService.RecordFailover(
						modelSelection.selectedModel,
						"embedded_completion_failed",
						embeddedRun.startedAtMs + 1);
					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = modelSelection.selectedModel,
						.errorCode = "embedded_completion_failed",
						.errorMessage = "embedded completion failed",
					};
				}

				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = true,
					.assistantText = assistantText,
					.modelId = modelSelection.selectedModel,
					.errorCode = {},
					.errorMessage = {},
				};
			});

		m_gatewayHost.SetChatAbortCallback([this](
			const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request) {
				if (m_activeChatProvider == "deepseek") {
					MarkDeepSeekRunCancelled(request.runId);
					return true;
				}

				if (!m_localModelActivationEnabled) {
					return false;
				}

				const bool cancelled = m_localModelRuntime.Cancel(request.runId);
				m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
				return cancelled;
			});

		m_gatewayHost.SetEmbeddingsGenerateCallback([this](
			const blazeclaw::gateway::GatewayHost::EmbeddingsGenerateRequest& request) {
				const auto result = m_embeddingsService.EmbedText(
					EmbeddingRequest{
						.text = ToWide(request.text),
						.normalize = request.normalize,
						.traceId = request.traceId,
					});

				blazeclaw::gateway::GatewayHost::EmbeddingsGenerateResult gatewayResult;
				gatewayResult.ok = result.ok;
				gatewayResult.vector = result.vector;
				gatewayResult.dimension = result.dimension;
				gatewayResult.provider = result.provider;
				gatewayResult.modelId = result.modelId;
				gatewayResult.latencyMs = result.latencyMs;
				gatewayResult.status = m_embeddingsService.Snapshot().status;

				if (result.error.has_value()) {
					gatewayResult.errorCode =
						EmbeddingErrorCodeToString(result.error->code);
					gatewayResult.errorMessage = result.error->message;
				}

				return gatewayResult;
			});

		m_gatewayHost.SetEmbeddingsBatchCallback([this](
			const blazeclaw::gateway::GatewayHost::EmbeddingsBatchRequest& request) {
				std::vector<std::wstring> texts;
				texts.reserve(request.texts.size());
				for (const auto& text : request.texts) {
					texts.push_back(ToWide(text));
				}

				const auto result = m_embeddingsService.EmbedBatch(
					EmbeddingBatchRequest{
						.texts = std::move(texts),
						.normalize = request.normalize,
						.traceId = request.traceId,
					});

				blazeclaw::gateway::GatewayHost::EmbeddingsBatchResult gatewayResult;
				gatewayResult.ok = result.ok;
				gatewayResult.vectors = result.vectors;
				gatewayResult.dimension = result.dimension;
				gatewayResult.provider = result.provider;
				gatewayResult.modelId = result.modelId;
				gatewayResult.latencyMs = result.latencyMs;
				gatewayResult.status = m_embeddingsService.Snapshot().status;

				if (result.error.has_value()) {
					gatewayResult.errorCode =
						EmbeddingErrorCodeToString(result.error->code);
					gatewayResult.errorMessage = result.error->message;
				}

				return gatewayResult;
			});

		const bool gatewayStarted = m_gatewayHost.Start(config.gateway);
		m_running = gatewayStarted;
		return m_running;
	}

	void ServiceManager::Stop() {
		m_skillsEnvOverrideService.RevertAll();
		m_gatewayHost.Stop();
		m_running = false;
	}

	bool ServiceManager::IsRunning() const noexcept {
		return m_running;
	}

	const FeatureRegistry& ServiceManager::Registry() const noexcept {
		return m_registry;
	}

	const AgentScopeSnapshot& ServiceManager::AgentsScope() const noexcept {
		return m_agentsScope;
	}

	const AgentsWorkspaceSnapshot& ServiceManager::AgentsWorkspace() const noexcept {
		return m_agentsWorkspace;
	}

	const SubagentRegistrySnapshot& ServiceManager::SubagentRegistry() const noexcept {
		return m_subagentRegistry;
	}

	const AcpSpawnDecision& ServiceManager::LastAcpDecision() const noexcept {
		return m_lastAcpDecision;
	}

	std::size_t ServiceManager::ActiveEmbeddedRuns() const noexcept {
		return m_piEmbeddedService.ActiveRuns();
	}

	const AgentsToolPolicySnapshot& ServiceManager::ToolPolicy() const noexcept {
		return m_agentsToolPolicy;
	}

	std::size_t ServiceManager::ShellProcessCount() const noexcept {
		return m_agentsShellRuntimeService.ListProcesses().size();
	}

	const ModelRoutingSnapshot& ServiceManager::ModelRouting() const noexcept {
		return m_modelRouting;
	}

	const AuthProfileSnapshot& ServiceManager::AuthProfiles() const noexcept {
		return m_authProfiles;
	}

	const SandboxSnapshot& ServiceManager::Sandbox() const noexcept {
		return m_sandbox;
	}

	const EmbeddingsServiceSnapshot& ServiceManager::Embeddings() const noexcept {
		return m_embeddings;
	}

	const localmodel::LocalModelRuntimeSnapshot& ServiceManager::LocalModelRuntime() const noexcept {
		return m_localModelRuntimeSnapshot;
	}

	bool ServiceManager::LocalModelRolloutEligible() const noexcept {
		return m_localModelRolloutEligible;
	}

	bool ServiceManager::LocalModelActivationEnabled() const noexcept {
		return m_localModelActivationEnabled;
	}

	const std::string& ServiceManager::LocalModelActivationReason() const noexcept {
		return m_localModelActivationReason;
	}

	const RetrievalMemorySnapshot& ServiceManager::RetrievalMemory() const noexcept {
		return m_retrievalMemory;
	}

	std::string ServiceManager::BuildOperatorDiagnosticsReport() const {
		const auto featureStateLabel = [](const FeatureState state) {
			switch (state) {
			case FeatureState::Implemented:
				return "implemented";
			case FeatureState::InProgress:
				return "in_progress";
			case FeatureState::Planned:
			default:
				return "planned";
			}
			};

		std::size_t implementedCount = 0;
		std::size_t inProgressCount = 0;
		std::size_t plannedCount = 0;
		for (const auto& feature : m_registry.Features()) {
			if (feature.state == FeatureState::Implemented) {
				++implementedCount;
				continue;
			}

			if (feature.state == FeatureState::InProgress) {
				++inProgressCount;
				continue;
			}

			++plannedCount;
		}

		const auto routing = ModelRouting();
		const auto auth = AuthProfiles();
		const auto sandbox = Sandbox();
		const auto embeddings = Embeddings();
		const auto localModel = LocalModelRuntime();
		const auto retrieval = RetrievalMemory();
		const bool selfEvolvingReminderInjected =
			m_skillsPrompt.prompt.find(L"## Self-Evolving Reminder") !=
			std::wstring::npos;
		const bool configFeatureImplemented =
			m_registry.IsImplemented(L"embeddings-config-foundation");

		std::string report =
			"{\"runtime\":{\"running\":" + std::string(m_running ? "true" : "false") +
			",\"gatewayWarning\":\"" + m_gatewayHost.LastWarning() + "\"},"
			"\"agents\":{\"count\":" + std::to_string(m_agentsScope.entries.size()) +
			",\"defaultAgent\":\"" + ToNarrow(m_agentsScope.defaultAgentId) + "\"},"
			"\"subagents\":{\"active\":" + std::to_string(m_subagentRegistry.activeRuns) +
			",\"pendingAnnounce\":" + std::to_string(m_subagentRegistry.pendingAnnounce) + "},"
			"\"acp\":{\"lastAllowed\":" +
			std::string(m_lastAcpDecision.allowed ? "true" : "false") +
			",\"reason\":\"" + m_lastAcpDecision.reason + "\"},"
			"\"embedded\":{\"activeRuns\":" + std::to_string(ActiveEmbeddedRuns()) + "},"
			"\"tools\":{\"policyEntries\":" + std::to_string(m_agentsToolPolicy.entries.size()) +
			",\"shellProcesses\":" + std::to_string(ShellProcessCount()) + "},"
			"\"modelAuth\":{\"primary\":\"" + routing.primaryModel +
			"\",\"fallback\":\"" + routing.fallbackModel +
			"\",\"failovers\":" + std::to_string(routing.failoverHistory.size()) +
			",\"authProfiles\":" + std::to_string(auth.entries.size()) + "},"
			"\"sandbox\":{\"enabledCount\":" + std::to_string(sandbox.enabledCount) +
			",\"browserEnabledCount\":" + std::to_string(sandbox.browserEnabledCount) + "},"
			"\"embeddings\":{\"enabled\":" +
			std::string(embeddings.enabled ? "true" : "false") +
			",\"ready\":" +
			std::string(embeddings.ready ? "true" : "false") +
			",\"provider\":\"" + embeddings.provider +
			"\",\"status\":\"" + embeddings.status +
			"\",\"dimension\":" + std::to_string(embeddings.dimension) +
			",\"maxSequenceLength\":" +
			std::to_string(embeddings.maxSequenceLength) +
			",\"modelPathConfigured\":" +
			std::string(!embeddings.modelPath.empty() ? "true" : "false") +
			",\"tokenizerPathConfigured\":" +
			std::string(!embeddings.tokenizerPath.empty() ? "true" : "false") +
			",\"configFeatureImplemented\":" +
			std::string(configFeatureImplemented ? "true" : "false") + "},"
			"\"localModel\":{\"enabled\":" +
			std::string(localModel.enabled ? "true" : "false") +
			",\"ready\":" +
			std::string(localModel.ready ? "true" : "false") +
			",\"rolloutEligible\":" +
			std::string(m_localModelRolloutEligible ? "true" : "false") +
			",\"activationEnabled\":" +
			std::string(m_localModelActivationEnabled ? "true" : "false") +
			",\"activationReason\":\"" + m_localModelActivationReason +
			",\"provider\":\"" + localModel.provider +
			"\",\"rolloutStage\":\"" + localModel.rolloutStage +
			"\",\"storageRoot\":\"" + localModel.storageRoot +
			"\",\"version\":\"" + localModel.version +
			"\",\"status\":\"" + localModel.status +
			"\",\"verboseMetrics\":" +
			std::string(localModel.verboseMetrics ? "true" : "false") +
			",\"runtimeDllPresent\":" +
			std::string(localModel.runtimeDllPresent ? "true" : "false") +
			",\"maxTokens\":" +
			std::to_string(localModel.maxTokens) +
			",\"temperature\":" +
			std::to_string(localModel.temperature) +
			",\"modelLoadAttempts\":" +
			std::to_string(localModel.modelLoadAttempts) +
			",\"modelLoadFailures\":" +
			std::to_string(localModel.modelLoadFailures) +
			",\"requestsStarted\":" +
			std::to_string(localModel.requestsStarted) +
			",\"requestsCompleted\":" +
			std::to_string(localModel.requestsCompleted) +
			",\"requestsFailed\":" +
			std::to_string(localModel.requestsFailed) +
			",\"requestsCancelled\":" +
			std::to_string(localModel.requestsCancelled) +
			",\"cumulativeTokens\":" +
			std::to_string(localModel.cumulativeTokens) +
			",\"cumulativeLatencyMs\":" +
			std::to_string(localModel.cumulativeLatencyMs) +
			",\"lastLatencyMs\":" +
			std::to_string(localModel.lastLatencyMs) +
			",\"lastGeneratedTokens\":" +
			std::to_string(localModel.lastGeneratedTokens) +
			",\"lastTokensPerSecond\":" +
			std::to_string(localModel.lastTokensPerSecond) +
			",\"modelPathConfigured\":" +
			std::string(!localModel.modelPath.empty() ? "true" : "false") +
			",\"modelHashConfigured\":" +
			std::string(!localModel.modelExpectedSha256.empty() ? "true" : "false") +
			",\"modelHashVerified\":" +
			std::string(localModel.modelHashVerified ? "true" : "false") +
			",\"tokenizerPathConfigured\":" +
			std::string(!localModel.tokenizerPath.empty() ? "true" : "false") +
			",\"tokenizerHashConfigured\":" +
			std::string(!localModel.tokenizerExpectedSha256.empty() ? "true" : "false") +
			",\"tokenizerHashVerified\":" +
			std::string(localModel.tokenizerHashVerified ? "true" : "false") +
			"},"
			"\"retrieval\":{\"enabled\":" +
			std::string(retrieval.enabled ? "true" : "false") +
			",\"recordCount\":" + std::to_string(retrieval.recordCount) +
			",\"lastQueryCount\":" + std::to_string(retrieval.lastQueryCount) +
			",\"status\":\"" + retrieval.status + "\"},"
			"\"skills\":{\"catalogEntries\":" + std::to_string(m_skillsCatalog.entries.size()) +
			",\"promptIncluded\":" + std::to_string(m_skillsPrompt.includedCount) +
			",\"selfEvolvingReminderInjected\":" +
			std::string(selfEvolvingReminderInjected ? "true" : "false") +
			"},"
			"\"hooks\":{\"loaded\":" +
			std::to_string(m_hookCatalog.diagnostics.hooksLoaded) +
			",\"engineMode\":\"" +
			WideToNarrowAscii(m_hookExecution.diagnostics.engineMode) + "\"" +
			",\"hookEngineEnabled\":" +
			std::string(m_hooksEngineEnabled ? "true" : "false") +
			",\"fallbackPromptInjection\":" +
			std::string(m_hooksFallbackPromptInjection ? "true" : "false") +
			",\"reminderEnabled\":" +
			std::string(m_hooksReminderEnabled ? "true" : "false") +
			",\"reminderVerbosity\":\"" +
			WideToNarrowAscii(m_hooksReminderVerbosity) + "\"" +
			",\"strictPolicyEnforcement\":" +
			std::string(m_hooksStrictPolicyEnforcement ? "true" : "false") +
			",\"allowedPackagesCount\":" +
			std::to_string(m_hooksAllowedPackages.size()) +
			",\"governanceReportingEnabled\":" +
			std::string(m_hooksGovernanceReportingEnabled ? "true" : "false") +
			",\"governanceReportsGenerated\":" +
			std::to_string(m_hooksGovernanceReportsGenerated) +
			",\"lastGovernanceReportPath\":\"" +
			WideToNarrowAscii(m_hooksLastGovernanceReportPath) + "\"" +
			",\"autoRemediationEnabled\":" +
			std::string(m_hooksAutoRemediationEnabled ? "true" : "false") +
			",\"autoRemediationRequiresApproval\":" +
			std::string(m_hooksAutoRemediationRequiresApproval ? "true" : "false") +
			",\"autoRemediationExecuted\":" +
			std::to_string(m_hooksAutoRemediationExecuted) +
			",\"lastAutoRemediationStatus\":\"" +
			WideToNarrowAscii(m_hooksLastAutoRemediationStatus) + "\"" +
			",\"autoRemediationTenantId\":\"" +
			WideToNarrowAscii(m_hooksAutoRemediationTenantId) + "\"" +
			",\"lastAutoRemediationPlaybookPath\":\"" +
			WideToNarrowAscii(m_hooksLastAutoRemediationPlaybookPath) + "\"" +
			",\"autoRemediationTokenMaxAgeMinutes\":" +
			std::to_string(m_hooksAutoRemediationTokenMaxAgeMinutes) +
			",\"autoRemediationTokenRotations\":" +
			std::to_string(m_hooksAutoRemediationTokenRotations) +
			",\"remediationTelemetryEnabled\":" +
			std::string(m_hooksRemediationTelemetryEnabled ? "true" : "false") +
			",\"remediationAuditEnabled\":" +
			std::string(m_hooksRemediationAuditEnabled ? "true" : "false") +
			",\"lastRemediationTelemetryPath\":\"" +
			WideToNarrowAscii(m_hooksLastRemediationTelemetryPath) + "\"" +
			",\"lastRemediationAuditPath\":\"" +
			WideToNarrowAscii(m_hooksLastRemediationAuditPath) + "\"" +
			",\"remediationSloStatus\":\"" +
			WideToNarrowAscii(m_hooksRemediationSloStatus) + "\"" +
			",\"remediationSloMaxDriftDetected\":" +
			std::to_string(m_hooksRemediationSloMaxDriftDetected) +
			",\"remediationSloMaxPolicyBlocked\":" +
			std::to_string(m_hooksRemediationSloMaxPolicyBlocked) +
			",\"complianceAttestationEnabled\":" +
			std::string(m_hooksComplianceAttestationEnabled ? "true" : "false") +
			",\"lastComplianceAttestationPath\":\"" +
			WideToNarrowAscii(m_hooksLastComplianceAttestationPath) + "\"" +
			",\"enterpriseSlaGovernanceEnabled\":" +
			std::string(m_hooksEnterpriseSlaGovernanceEnabled ? "true" : "false") +
			",\"enterpriseSlaPolicyId\":\"" +
			WideToNarrowAscii(m_hooksEnterpriseSlaPolicyId) + "\"" +
			",\"crossTenantAttestationAggregationEnabled\":" +
			std::string(m_hooksCrossTenantAttestationAggregationEnabled ? "true" : "false") +
			",\"crossTenantAttestationAggregationStatus\":\"" +
			WideToNarrowAscii(m_hooksCrossTenantAttestationAggregationStatus) + "\"" +
			",\"crossTenantAttestationAggregationCount\":" +
			std::to_string(m_hooksCrossTenantAttestationAggregationCount) +
			",\"lastCrossTenantAttestationAggregationPath\":\"" +
			WideToNarrowAscii(m_hooksLastCrossTenantAttestationAggregationPath) + "\"" +
			",\"selfEvolvingHookTriggered\":" +
			std::string(m_selfEvolvingHookTriggered ? "true" : "false") +
			",\"invalidMetadata\":" +
			std::to_string(m_hookCatalog.diagnostics.invalidMetadataFiles) +
			",\"unsafeHandlerPaths\":" +
			std::to_string(m_hookCatalog.diagnostics.unsafeHandlerPaths) +
			",\"missingHandlers\":" +
			std::to_string(m_hookCatalog.diagnostics.missingHandlerFiles) +
			",\"eventsEmitted\":" +
			std::to_string(m_hookEvents.diagnostics.emittedCount) +
			",\"eventValidationFailed\":" +
			std::to_string(m_hookEvents.diagnostics.validationFailedCount) +
			",\"eventsDropped\":" +
			std::to_string(m_hookEvents.diagnostics.droppedCount) +
			",\"dispatches\":" +
			std::to_string(m_hookExecution.diagnostics.dispatchCount) +
			",\"hookDispatchCount\":" +
			std::to_string(m_hookExecution.diagnostics.dispatchCount) +
			",\"dispatchSuccess\":" +
			std::to_string(m_hookExecution.diagnostics.successCount) +
			",\"dispatchFailures\":" +
			std::to_string(m_hookExecution.diagnostics.failureCount) +
			",\"hookFailureCount\":" +
			std::to_string(m_hookExecution.diagnostics.failureCount) +
			",\"dispatchSkipped\":" +
			std::to_string(m_hookExecution.diagnostics.skippedCount) +
			",\"dispatchTimeouts\":" +
			std::to_string(m_hookExecution.diagnostics.timeoutCount) +
			",\"guardRejected\":" +
			std::to_string(m_hookExecution.diagnostics.guardRejectedCount) +
			",\"reminderTriggered\":" +
			std::to_string(m_hookExecution.diagnostics.reminderTriggeredCount) +
			",\"reminderInjected\":" +
			std::to_string(m_hookExecution.diagnostics.reminderInjectedCount) +
			",\"reminderSkipped\":" +
			std::to_string(m_hookExecution.diagnostics.reminderSkippedCount) +
			",\"policyBlocked\":" +
			std::to_string(m_hookExecution.diagnostics.policyBlockedCount) +
			",\"driftDetected\":" +
			std::to_string(m_hookExecution.diagnostics.driftDetectedCount) +
			",\"lastDriftReason\":\"" +
			WideToNarrowAscii(m_hookExecution.diagnostics.lastDriftReason) + "\"" +
			",\"reminderState\":\"" +
			WideToNarrowAscii(m_hookExecution.diagnostics.lastReminderState) + "\"" +
			",\"reminderReason\":\"" +
			WideToNarrowAscii(m_hookExecution.diagnostics.lastReminderReason) + "\"" +
			"},"
			"\"features\":{\"implemented\":" + std::to_string(implementedCount) +
			",\"inProgress\":" + std::to_string(inProgressCount) +
			",\"planned\":" + std::to_string(plannedCount) +
			",\"registryState\":\"" + featureStateLabel(
				m_registry.Features().empty() ? FeatureState::Planned : m_registry.Features().front().state) +
			"\"}}";

		return report;
	}

	void ServiceManager::SetActiveChatProvider(
		const std::string& provider,
		const std::string& model) {
		m_activeChatProvider = provider.empty() ? "local" : provider;
		m_activeChatModel = model.empty() ? "default" : model;
	}

	const std::string& ServiceManager::ActiveChatProvider() const noexcept {
		return m_activeChatProvider;
	}

	const std::string& ServiceManager::ActiveChatModel() const noexcept {
		return m_activeChatModel;
	}

	std::optional<std::string> ServiceManager::ResolveDeepSeekCredentialUtf8() const {
		if (const auto cred = blazeclaw::app::CredentialStore::LoadCredential(
			L"blazeclaw.deepseek");
			cred.has_value() && !cred->empty()) {
			return cred;
		}

		wchar_t appdataBuf[MAX_PATH];
		if (GetEnvironmentVariableW(
			L"APPDATA",
			appdataBuf,
			static_cast<DWORD>(std::size(appdataBuf))) > 0) {
			const std::wstring dpPath =
				std::wstring(appdataBuf) + L"\\BlazeClaw\\deepseek.key";
			if (const auto dp = blazeclaw::app::CredentialStore::LoadCredentialDPAPI(
				dpPath);
				dp.has_value() && !dp->empty()) {
				return dp;
			}
		}

		return std::nullopt;
	}

	bool ServiceManager::HasDeepSeekCredential() const {
		const auto cred = ResolveDeepSeekCredentialUtf8();
		return cred.has_value() && !cred->empty();
	}

	bool ServiceManager::IsDeepSeekRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_deepSeekCancelMutex);
		const auto it = m_deepSeekCancelledRuns.find(runId);
		return it != m_deepSeekCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		EmitDeepSeekDiagnostic(
			"cancel",
			std::string("mark cancelled runId=") + runId);

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.erase(runId);
	}

	std::optional<std::string> ServiceManager::ExtractDeepSeekAssistantText(
		const std::string& responseJson) const {
		std::string choicesRaw;
		if (!blazeclaw::gateway::json::FindRawField(responseJson, "choices", choicesRaw)) {
			return std::nullopt;
		}

		const std::string contentKey = "\"content\":\"";
		const auto contentPos = choicesRaw.find(contentKey);
		if (contentPos == std::string::npos) {
			return std::nullopt;
		}

		const std::size_t start = contentPos + contentKey.size();
		std::string text;
		text.reserve(256);
		bool escaping = false;
		for (std::size_t i = start; i < choicesRaw.size(); ++i) {
			const char ch = choicesRaw[i];
			if (escaping) {
				switch (ch) {
				case 'n':
					text.push_back('\n');
					break;
				case 'r':
					text.push_back('\r');
					break;
				case 't':
					text.push_back('\t');
					break;
				case '"':
				case '\\':
				case '/':
					text.push_back(ch);
					break;
				default:
					text.push_back(ch);
					break;
				}
				escaping = false;
				continue;
			}

			if (ch == '\\') {
				escaping = true;
				continue;
			}

			if (ch == '"') {
				break;
			}

			text.push_back(ch);
		}

		if (text.empty()) {
			return std::nullopt;
		}

		return text;
	}

	std::vector<std::string> ServiceManager::ExtractDeepSeekAssistantDeltas(
		const std::string& responseBody) const {
		std::vector<std::string> deltas;
		std::string cumulative;

		std::size_t cursor = 0;
		while (cursor < responseBody.size()) {
			const std::size_t lineEnd = responseBody.find('\n', cursor);
			const std::size_t end = lineEnd == std::string::npos ? responseBody.size() : lineEnd;
			std::string line = responseBody.substr(cursor, end - cursor);
			cursor = lineEnd == std::string::npos ? responseBody.size() : lineEnd + 1;

			if (!line.empty() && line.back() == '\r') {
				line.pop_back();
			}

			const std::string prefix = "data:";
			if (line.rfind(prefix, 0) != 0) {
				continue;
			}

			std::string payload = blazeclaw::gateway::json::Trim(line.substr(prefix.size()));
			if (payload.empty() || payload == "[DONE]") {
				continue;
			}

			std::string choicesRaw;
			if (!blazeclaw::gateway::json::FindRawField(payload, "choices", choicesRaw)) {
				continue;
			}

			const std::string contentKey = "\"content\":\"";
			const auto contentPos = choicesRaw.find(contentKey);
			if (contentPos == std::string::npos) {
				continue;
			}

			const std::size_t start = contentPos + contentKey.size();
			std::string piece;
			bool escaping = false;
			for (std::size_t i = start; i < choicesRaw.size(); ++i) {
				const char ch = choicesRaw[i];
				if (escaping) {
					switch (ch) {
					case 'n':
						piece.push_back('\n');
						break;
					case 'r':
						piece.push_back('\r');
						break;
					case 't':
						piece.push_back('\t');
						break;
					case '"':
					case '\\':
					case '/':
						piece.push_back(ch);
						break;
					default:
						piece.push_back(ch);
						break;
					}
					escaping = false;
					continue;
				}

				if (ch == '\\') {
					escaping = true;
					continue;
				}

				if (ch == '"') {
					break;
				}

				piece.push_back(ch);
			}

			if (piece.empty()) {
				continue;
			}

			cumulative += piece;
			deltas.push_back(cumulative);
		}

		return deltas;
	}

	std::optional<std::string> ServiceManager::ExtractDeepSeekErrorMessage(
		const std::string& responseJson) const {
		std::string errorRaw;
		if (!blazeclaw::gateway::json::FindRawField(responseJson, "error", errorRaw)) {
			return std::nullopt;
		}

		std::string message;
		if (blazeclaw::gateway::json::FindStringField(errorRaw, "message", message) &&
			!message.empty()) {
			return message;
		}

		return std::string("DeepSeek request failed.");
	}

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult
		ServiceManager::InvokeDeepSeekRemoteChat(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& modelId,
			const std::string& apiKey) const {
		const auto startedAt = std::chrono::steady_clock::now();
		const std::string deepSeekBaseUrl = "https://api.deepseek.com";
		const std::string endpoint = deepSeekBaseUrl + "/chat/completions";

		EmitDeepSeekDiagnostic(
			"connect",
			std::string("begin runId=") +
			request.runId +
			" session=" +
			request.sessionKey +
			" model=" +
			modelId +
			" stream=true endpoint=" +
			endpoint);

		std::wstring host;
		std::wstring path;
		INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
		bool secure = true;
		if (const auto parseError = ParseHttpsUrl(endpoint, host, path, port, secure);
			parseError.has_value()) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("invalid endpoint runId=") +
				request.runId +
				" code=" +
				parseError.value());
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = parseError.value(),
				.errorMessage = "DeepSeek endpoint URL is invalid.",
			};
		}

		HINTERNET session = WinHttpOpen(
			L"BlazeClaw/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (session == nullptr) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("WinHttpOpen failed runId=") + request.runId);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_open_failed",
				.errorMessage = "Failed to initialize DeepSeek HTTP session.",
			};
		}

		auto closeSession = [&session]() {
			if (session != nullptr) {
				WinHttpCloseHandle(session);
				session = nullptr;
			}
			};

		HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
		if (connection == nullptr) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("WinHttpConnect failed runId=") + request.runId);
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_connect_failed",
				.errorMessage = "Failed to connect to DeepSeek endpoint.",
			};
		}

		auto closeConnection = [&connection]() {
			if (connection != nullptr) {
				WinHttpCloseHandle(connection);
				connection = nullptr;
			}
			};

		HINTERNET requestHandle = WinHttpOpenRequest(
			connection,
			L"POST",
			path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			secure ? WINHTTP_FLAG_SECURE : 0);
		if (requestHandle == nullptr) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("WinHttpOpenRequest failed runId=") + request.runId);
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_request_failed",
				.errorMessage = "Failed to create DeepSeek request.",
			};
		}

		auto closeRequest = [&requestHandle]() {
			if (requestHandle != nullptr) {
				WinHttpCloseHandle(requestHandle);
				requestHandle = nullptr;
			}
			};

		const std::wstring authHeaderW =
			L"Authorization: Bearer " + ToWide(apiKey) + L"\r\n";
		const std::wstring contentTypeW =
			L"Content-Type: application/json\r\n";
		const std::wstring allHeadersW = authHeaderW + contentTypeW;

		const std::string escapedMessage = EscapeJsonUtf8(request.message);
		const std::string payload =
			std::string("{\"model\":\"") + modelId +
			"\",\"stream\":true,\"messages\":[{\"role\":\"user\",\"content\":\"" +
			escapedMessage + "\"}]}";

		if (!WinHttpSendRequest(
			requestHandle,
			allHeadersW.c_str(),
			static_cast<DWORD>(allHeadersW.size()),
			(LPVOID)payload.data(),
			static_cast<DWORD>(payload.size()),
			static_cast<DWORD>(payload.size()),
			0)) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("WinHttpSendRequest failed runId=") + request.runId);
			closeRequest();
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_send_failed",
				.errorMessage = "DeepSeek request transmission failed.",
			};
		}

		if (!WinHttpReceiveResponse(requestHandle, nullptr)) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("WinHttpReceiveResponse failed runId=") + request.runId);
			closeRequest();
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_receive_failed",
				.errorMessage = "DeepSeek response reception failed.",
			};
		}

		DWORD statusCode = 0;
		DWORD statusCodeSize = sizeof(statusCode);
		WinHttpQueryHeaders(
			requestHandle,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusCodeSize,
			WINHTTP_NO_HEADER_INDEX);
		EmitDeepSeekDiagnostic(
			"connect",
			std::string("response headers runId=") +
			request.runId +
			" status=" +
			std::to_string(statusCode));

		std::string responseBody;
		std::size_t chunkCount = 0;
		while (true) {
			if (IsDeepSeekRunCancelled(request.runId)) {
				EmitDeepSeekDiagnostic(
					"cancel",
					std::string("cancel observed runId=") + request.runId);
				closeRequest();
				closeConnection();
				closeSession();
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = modelId,
					.errorCode = "deepseek_request_cancelled",
					.errorMessage = "DeepSeek request cancelled.",
				};
			}

			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(requestHandle, &available)) {
				break;
			}
			if (available == 0) {
				break;
			}

			std::string chunk(static_cast<std::size_t>(available), '\0');
			DWORD downloaded = 0;
			if (!WinHttpReadData(
				requestHandle,
				chunk.data(),
				available,
				&downloaded)) {
				EmitDeepSeekDiagnostic(
					"error",
					std::string("WinHttpReadData failed runId=") + request.runId);
				break;
			}

			responseBody.append(chunk.data(), static_cast<std::size_t>(downloaded));
			++chunkCount;
		}

		EmitDeepSeekDiagnostic(
			"stream",
			std::string("read completed runId=") +
			request.runId +
			" chunks=" +
			std::to_string(chunkCount) +
			" bytes=" +
			std::to_string(responseBody.size()));

		closeRequest();
		closeConnection();
		closeSession();

		if (statusCode < 200 || statusCode >= 300) {
			const std::string errorMessage = ExtractDeepSeekErrorMessage(responseBody)
				.value_or("DeepSeek service returned an error.");
			EmitDeepSeekDiagnostic(
				"error",
				std::string("status error runId=") +
				request.runId +
				" status=" +
				std::to_string(statusCode) +
				" message=" +
				errorMessage);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = modelId,
				.errorCode = "deepseek_http_status_error",
				.errorMessage = errorMessage,
			};
		}

		const auto assistantDeltas = ExtractDeepSeekAssistantDeltas(responseBody);
		const std::string assistantText = assistantDeltas.empty()
			? std::string()
			: assistantDeltas.back();

		EmitDeepSeekDiagnostic(
			"stream",
			std::string("delta parsed runId=") +
			request.runId +
			" snapshots=" +
			std::to_string(assistantDeltas.size()) +
			" finalChars=" +
			std::to_string(assistantText.size()));

		if (assistantText.empty()) {
			EmitDeepSeekDiagnostic(
				"error",
				std::string("invalid response runId=") + request.runId);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.assistantDeltas = {},
				.modelId = modelId,
				.errorCode = "deepseek_invalid_response",
				.errorMessage = "DeepSeek response did not contain assistant text.",
			};
		}

		const auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - startedAt)
			.count();
		EmitDeepSeekDiagnostic(
			"final",
			std::string("completed runId=") +
			request.runId +
			" latencyMs=" +
			std::to_string(latencyMs) +
			" finalChars=" +
			std::to_string(assistantText.size()));

		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = true,
			.assistantText = assistantText,
			.assistantDeltas = assistantDeltas,
			.modelId = modelId,
			.errorCode = {},
			.errorMessage = {},
		};
	}

	const SkillsCatalogSnapshot& ServiceManager::SkillsCatalog() const noexcept {
		return m_skillsCatalog;
	}

	const SkillsEligibilitySnapshot& ServiceManager::SkillsEligibility() const noexcept {
		return m_skillsEligibility;
	}

	const SkillsPromptSnapshot& ServiceManager::SkillsPrompt() const noexcept {
		return m_skillsPrompt;
	}

	std::string ServiceManager::InvokeGatewayMethod(
		const std::string& method,
		const std::optional<std::string>& paramsJson) const {
		const blazeclaw::gateway::protocol::RequestFrame request{
			.id = "ui-probe",
			.method = method,
			.paramsJson = paramsJson,
		};

		const auto response = RouteGatewayRequest(request);
		if (response.ok) {
			return response.payloadJson.has_value() ? response.payloadJson.value()
				: "ok";
		}

		if (!response.error.has_value()) {
			return "error_unknown";
		}

		const auto& error = response.error.value();
		return error.code + ":" + error.message;
	}

	blazeclaw::gateway::protocol::ResponseFrame ServiceManager::RouteGatewayRequest(
		const blazeclaw::gateway::protocol::RequestFrame& request) const {
		if (!m_running) {
			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "service_not_running",
					.message = "Service manager is not running.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
		}

		if (request.method.empty()) {
			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "invalid_method",
					.message = "Gateway method must not be empty.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
		}

		return m_gatewayHost.RouteRequest(request);
	}

	bool ServiceManager::PumpGatewayNetworkOnce(std::string& error) {
		if (!m_running) {
			error = "service manager is not running";
			return false;
		}

		return m_gatewayHost.PumpNetworkOnce(error);
	}

	bool ServiceManager::IsLocalModelRolloutEligible() const {
		const std::wstring stage = m_activeConfig.localModel.rolloutStage;
		if (_wcsicmp(stage.c_str(), L"stable") == 0) {
			return true;
		}

		if (_wcsicmp(stage.c_str(), L"nightly") == 0) {
			return IsOneOfChannels(m_activeConfig.enabledChannels, L"nightly");
		}

		return true;
	}

} // namespace blazeclaw::core
