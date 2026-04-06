#include "pch.h"
#include "ServiceManager.h"
#include "../app/CredentialStore.h"
#include "../app/BlazeClawMFCDoc.h"
#include "../app/BlazeClawMFCView.h"
#include "../app/MainFrame.h"

#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/executors/EmailScheduleExecutor.h"

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
#include <nlohmann/json.hpp>

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

		void AppendStartupTrace(const char* stage) {
			if (stage == nullptr || *stage == '\0') {
				return;
			}

			wchar_t tempPath[MAX_PATH]{};
			const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
			std::filesystem::path logPath;
			if (tempLength > 0 && tempLength < MAX_PATH) {
				logPath = std::filesystem::path(tempPath) /
					L"BlazeClaw.startup.trace.log";
			}
			else {
				logPath = std::filesystem::current_path() /
					L"BlazeClaw.startup.trace.log";
			}

			std::ofstream output(logPath, std::ios::app);
			if (!output.is_open()) {
				return;
			}

			output
				<< "pid=" << static_cast<unsigned long>(GetCurrentProcessId())
				<< " tick=" << static_cast<unsigned long long>(GetTickCount64())
				<< " stage=" << stage
				<< "\n";
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

		std::string ToLowerAscii(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::filesystem::path ResolveWorkspaceRootForSkills(
			const std::filesystem::path& startPath) {
			std::error_code ec;
			auto cursor = std::filesystem::absolute(startPath, ec);
			if (ec) {
				return startPath;
			}

			while (!cursor.empty()) {
				const auto directSkills = cursor / L"skills";
				if (std::filesystem::is_directory(directSkills, ec) && !ec) {
					return cursor;
				}

				const auto nestedSkills = cursor / L"blazeclaw" / L"skills";
				if (std::filesystem::is_directory(nestedSkills, ec) && !ec) {
					return cursor;
				}

				if (!cursor.has_parent_path()) {
					break;
				}

				auto parent = cursor.parent_path();
				if (parent == cursor) {
					break;
				}

				cursor = parent;
			}

			return startPath;
		}

		std::vector<std::string> ParseCsvEnvValues(const wchar_t* key) {
			std::vector<std::string> values;
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
					const auto trimmed = Trim(token);
					if (!trimmed.empty()) {
						values.push_back(ToNarrow(trimmed));
					}
					token.clear();
					continue;
				}

				token.push_back(env[i]);
			}

			const auto trimmed = Trim(token);
			if (!trimmed.empty()) {
				values.push_back(ToNarrow(trimmed));
			}

			free(env);
			return values;
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

			const std::wstring trimmed = Trim(rawValue);
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

		double ParseDoubleEnvValue(const wchar_t* key, const double fallback) {
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

			const std::wstring trimmed = Trim(rawValue);
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

		bool ContainsCaseInsensitive(
			const std::vector<std::string>& values,
			const std::string& candidate) {
			if (candidate.empty()) {
				return false;
			}

			const std::string loweredCandidate = ToLowerAscii(candidate);
			for (const auto& value : values) {
				if (ToLowerAscii(value) == loweredCandidate) {
					return true;
				}
			}

			return false;
		}

		std::wstring TrimWide(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLowerWide(std::wstring value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return value;
		}

		std::vector<std::wstring> SplitCommaDelimitedWide(
			const std::wstring& rawValue) {
			std::vector<std::wstring> values;
			std::wstring token;
			for (const wchar_t ch : rawValue) {
				if (ch == L',' || ch == L';') {
					const std::wstring trimmed = TrimWide(token);
					if (!trimmed.empty()) {
						values.push_back(trimmed);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::wstring trimmed = TrimWide(token);
			if (!trimmed.empty()) {
				values.push_back(trimmed);
			}

			return values;
		}

		const std::wstring* FindFrontmatterFieldCaseInsensitive(
			const SkillFrontmatter& frontmatter,
			const std::wstring& key) {
			const std::wstring loweredKey = ToLowerWide(key);
			for (const auto& item : frontmatter.fields) {
				if (ToLowerWide(item.first) == loweredKey) {
					return &item.second;
				}
			}

			return nullptr;
		}

		const std::wstring* ResolveNormalizedField(
			const SkillFrontmatter& frontmatter,
			const std::vector<std::wstring>& blazeclawKeys,
			const std::vector<std::wstring>& openclawKeys,
			std::vector<std::string>& outSources) {
			for (const auto& key : blazeclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.blazeclaw");
					return value;
				}
			}

			for (const auto& key : openclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.openclaw");
					return value;
				}
			}

			return nullptr;
		}

		std::vector<std::string> UniqueNarrowValues(
			const std::vector<std::wstring>& values) {
			std::vector<std::string> output;
			for (const auto& value : values) {
				const std::string narrow = ToNarrow(value);
				if (narrow.empty()) {
					continue;
				}

				if (std::find(output.begin(), output.end(), narrow) != output.end()) {
					continue;
				}

				output.push_back(narrow);
			}

			return output;
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

		struct ImapSmtpToolRuntimeSpec {
			std::string id;
			std::string label;
			std::string script;
			std::string command;
		};

		struct BraveSearchToolRuntimeSpec {
			std::string id;
			std::string label;
			std::string script;
		};

		struct BaiduSearchToolRuntimeSpec {
			std::string id;
			std::string label;
			std::string script;
		};

		struct ChildProcessResult {
			bool started = false;
			bool timedOut = false;
			DWORD exitCode = static_cast<DWORD>(-1);
			std::string output;
			std::string errorCode;
			std::string errorMessage;
		};

		std::vector<ImapSmtpToolRuntimeSpec> BuildImapSmtpToolRuntimeSpecs() {
			return {
				{ "imap_smtp_email.imap.check", "IMAP Check", "scripts/imap.js", "check" },
				{ "imap_smtp_email.imap.fetch", "IMAP Fetch", "scripts/imap.js", "fetch" },
				{ "imap_smtp_email.imap.download", "IMAP Download Attachments", "scripts/imap.js", "download" },
				{ "imap_smtp_email.imap.search", "IMAP Search", "scripts/imap.js", "search" },
				{ "imap_smtp_email.imap.mark_read", "IMAP Mark Read", "scripts/imap.js", "mark-read" },
				{ "imap_smtp_email.imap.mark_unread", "IMAP Mark Unread", "scripts/imap.js", "mark-unread" },
				{ "imap_smtp_email.imap.list_mailboxes", "IMAP List Mailboxes", "scripts/imap.js", "list-mailboxes" },
				{ "imap_smtp_email.imap.list_accounts", "IMAP List Accounts", "scripts/imap.js", "list-accounts" },
				{ "imap_smtp_email.smtp.send", "SMTP Send", "scripts/smtp.js", "send" },
				{ "imap_smtp_email.smtp.test", "SMTP Test", "scripts/smtp.js", "test" },
				{ "imap_smtp_email.smtp.list_accounts", "SMTP List Accounts", "scripts/smtp.js", "list-accounts" },
			};
		}

		std::vector<BraveSearchToolRuntimeSpec> BuildBraveSearchToolRuntimeSpecs() {
			return {
				{ "brave_search.search.web", "Brave Web Search", "scripts/search.js" },
				{ "brave_search.fetch.content", "Brave Fetch Content", "scripts/content.js" },
			};
		}

		std::vector<BaiduSearchToolRuntimeSpec> BuildBaiduSearchToolRuntimeSpecs() {
			return {
				{ "baidu_search.search.web", "Baidu Web Search", "scripts/search.py" },
			};
		}

		std::string TrimAsciiForBraveSearch(const std::string& value) {
			const std::size_t first = value.find_first_not_of(" \t\r\n");
			if (first == std::string::npos) {
				return {};
			}

			const std::size_t last = value.find_last_not_of(" \t\r\n");
			return value.substr(first, last - first + 1);
		}

		bool HasControlCharsForBraveSearch(const std::string& value) {
			for (const unsigned char ch : value) {
				if ((ch < 0x20 && ch != '\t') || ch == 0x7F) {
					return true;
				}
			}

			return false;
		}

		bool IsHttpUrlForBraveSearch(const std::string& value) {
			const std::string lowered = ToLowerAscii(value);
			return lowered.rfind("http://", 0) == 0 ||
				lowered.rfind("https://", 0) == 0;
		}

		bool HasEnvVarValue(const wchar_t* key) {
			wchar_t* value = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&value, &len, key) != 0 || value == nullptr || len == 0) {
				if (value != nullptr) {
					free(value);
				}
				return false;
			}

			const std::wstring trimmed = Trim(value);
			free(value);
			return !trimmed.empty();
		}

		bool ResolveBraveRequireApiKey() {
			return ReadBoolEnvOrDefault(L"BLAZECLAW_BRAVE_REQUIRE_API_KEY", false);
		}

		std::string TruncateBraveToolOutput(
			const std::string& output,
			const std::size_t maxBytes = 24000) {
			if (output.size() <= maxBytes) {
				return output;
			}

			const std::size_t retained = maxBytes > 48 ? maxBytes - 48 : maxBytes;
			return output.substr(0, retained) +
				"\n...(truncated by blazeclaw runtime output limit)";
		}

		std::string ClassifyBraveFailureCode(const std::string& output) {
			const std::string lowered = ToLowerAscii(output);
			if (lowered.find("http 401") != std::string::npos ||
				lowered.find("http 403") != std::string::npos) {
				return "auth_error";
			}

			if (lowered.find("http 429") != std::string::npos) {
				return "rate_limited";
			}

			if (lowered.find("http 500") != std::string::npos ||
				lowered.find("http 502") != std::string::npos ||
				lowered.find("http 503") != std::string::npos ||
				lowered.find("http 504") != std::string::npos) {
				return "upstream_unavailable";
			}

			if (lowered.find("enotfound") != std::string::npos ||
				lowered.find("eai_again") != std::string::npos ||
				lowered.find("network") != std::string::npos ||
				lowered.find("fetch failed") != std::string::npos) {
				return "network_error";
			}

			return "process_exit_nonzero";
		}

		std::wstring QuoteCommandToken(const std::wstring& token) {
			if (token.find_first_of(L" \t\"") == std::wstring::npos) {
				return token;
			}

			std::wstring quoted = L"\"";
			for (const wchar_t ch : token) {
				if (ch == L'\"') {
					quoted += L"\\\"";
				}
				else {
					quoted.push_back(ch);
				}
			}
			quoted += L"\"";
			return quoted;
		}

		std::wstring BuildCommandLine(const std::vector<std::wstring>& tokens) {
			std::wstring commandLine;
			for (std::size_t i = 0; i < tokens.size(); ++i) {
				if (i > 0) {
					commandLine += L" ";
				}

				commandLine += QuoteCommandToken(tokens[i]);
			}

			return commandLine;
		}

		std::string ReadPipeAll(HANDLE readPipe) {
			std::string output;
			if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE) {
				return output;
			}

			char buffer[4096]{};
			DWORD bytesRead = 0;
			while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) &&
				bytesRead > 0) {
				output.append(buffer, buffer + bytesRead);
			}

			return output;
		}

		ChildProcessResult ExecuteNodeSkillProcess(
			const std::filesystem::path& scriptPath,
			const std::vector<std::string>& cliArgs,
			const std::uint64_t timeoutMs) {
			ChildProcessResult result;

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			security.lpSecurityDescriptor = nullptr;

			HANDLE outputRead = nullptr;
			HANDLE outputWrite = nullptr;
			if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
				result.errorCode = "pipe_create_failed";
				result.errorMessage = "failed to create child process pipes";
				return result;
			}

			SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = outputWrite;
			startupInfo.hStdError = outputWrite;

			PROCESS_INFORMATION processInfo{};

			std::vector<std::wstring> commandTokens;
			commandTokens.push_back(L"node");
			commandTokens.push_back(scriptPath.wstring());
			for (const auto& arg : cliArgs) {
				commandTokens.push_back(ToWide(arg));
			}

			std::wstring commandLine = BuildCommandLine(commandTokens);
			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			const BOOL created = CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startupInfo,
				&processInfo);

			CloseHandle(outputWrite);

			if (!created) {
				result.errorCode = "process_start_failed";
				result.errorMessage = "failed to start node process";
				result.output = ReadPipeAll(outputRead);
				CloseHandle(outputRead);
				return result;
			}

			result.started = true;
			const DWORD waitMs = timeoutMs > static_cast<std::uint64_t>(MAXDWORD)
				? MAXDWORD
				: static_cast<DWORD>(timeoutMs);
			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, waitMs);

			if (waitResult == WAIT_TIMEOUT) {
				TerminateProcess(processInfo.hProcess, 124);
				result.timedOut = true;
				result.errorCode = "deadline_exceeded";
				result.errorMessage = "tool process exceeded execution deadline";
			}

			GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
			result.output = ReadPipeAll(outputRead);

			CloseHandle(outputRead);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);

			return result;
		}

		ChildProcessResult ExecutePythonSkillProcess(
			const std::filesystem::path& scriptPath,
			const std::vector<std::string>& cliArgs,
			const std::uint64_t timeoutMs) {
			ChildProcessResult result;

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			security.lpSecurityDescriptor = nullptr;

			HANDLE outputRead = nullptr;
			HANDLE outputWrite = nullptr;
			if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
				result.errorCode = "pipe_create_failed";
				result.errorMessage = "failed to create child process pipes";
				return result;
			}

			SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = outputWrite;
			startupInfo.hStdError = outputWrite;

			PROCESS_INFORMATION processInfo{};

			std::vector<std::wstring> commandTokens;
			commandTokens.push_back(L"python");
			commandTokens.push_back(scriptPath.wstring());
			for (const auto& arg : cliArgs) {
				commandTokens.push_back(ToWide(arg));
			}

			std::wstring commandLine = BuildCommandLine(commandTokens);
			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			const BOOL created = CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startupInfo,
				&processInfo);

			CloseHandle(outputWrite);

			if (!created) {
				result.errorCode = "process_start_failed";
				result.errorMessage = "failed to start python process";
				result.output = ReadPipeAll(outputRead);
				CloseHandle(outputRead);
				return result;
			}

			result.started = true;
			const DWORD waitMs = timeoutMs > static_cast<std::uint64_t>(MAXDWORD)
				? MAXDWORD
				: static_cast<DWORD>(timeoutMs);
			const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, waitMs);

			if (waitResult == WAIT_TIMEOUT) {
				TerminateProcess(processInfo.hProcess, 124);
				result.timedOut = true;
				result.errorCode = "deadline_exceeded";
				result.errorMessage = "tool process exceeded execution deadline";
			}

			GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
			result.output = ReadPipeAll(outputRead);

			CloseHandle(outputRead);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);

			return result;
		}

		std::optional<std::filesystem::path> ResolveImapSmtpSkillRoot() {
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(std::filesystem::current_path());

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
				candidates.push_back(std::filesystem::path(modulePath).parent_path());
			}

			for (const auto& root : candidates) {
				std::filesystem::path cursor = root;
				while (!cursor.empty()) {
					const auto candidate =
						cursor /
						L"blazeclaw" /
						L"skills" /
						L"imap-smtp-email";
					if (std::filesystem::exists(candidate / L"scripts" / L"imap.js") &&
						std::filesystem::exists(candidate / L"scripts" / L"smtp.js")) {
						return candidate;
					}

					if (!cursor.has_parent_path()) {
						break;
					}

					auto parent = cursor.parent_path();
					if (parent == cursor) {
						break;
					}

					cursor = parent;
				}
			}

			return std::nullopt;
		}

		std::optional<std::filesystem::path> ResolveBaiduSearchSkillRoot() {
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(std::filesystem::current_path());

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
				candidates.push_back(std::filesystem::path(modulePath).parent_path());
			}

			for (const auto& root : candidates) {
				std::filesystem::path cursor = root;
				while (!cursor.empty()) {
					const auto candidate =
						cursor /
						L"blazeclaw" /
						L"skills" /
						L"baidu-search";
					if (std::filesystem::exists(candidate / L"scripts" / L"search.py")) {
						return candidate;
					}

					if (!cursor.has_parent_path()) {
						break;
					}

					auto parent = cursor.parent_path();
					if (parent == cursor) {
						break;
					}

					cursor = parent;
				}
			}

			return std::nullopt;
		}

		std::optional<std::filesystem::path> ResolveBraveSearchSkillRoot() {
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(std::filesystem::current_path());

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
				candidates.push_back(std::filesystem::path(modulePath).parent_path());
			}

			for (const auto& root : candidates) {
				std::filesystem::path cursor = root;
				while (!cursor.empty()) {
					const auto candidate =
						cursor /
						L"blazeclaw" /
						L"skills" /
						L"brave-search";
					if (std::filesystem::exists(candidate / L"scripts" / L"search.js") &&
						std::filesystem::exists(candidate / L"scripts" / L"content.js")) {
						return candidate;
					}

					if (!cursor.has_parent_path()) {
						break;
					}

					auto parent = cursor.parent_path();
					if (parent == cursor) {
						break;
					}

					cursor = parent;
				}
			}

			return std::nullopt;
		}

		std::optional<std::string> JsonValueToCliString(const nlohmann::json& value) {
			if (value.is_string()) {
				return value.get<std::string>();
			}

			if (value.is_boolean()) {
				return value.get<bool>() ? "true" : "false";
			}

			if (value.is_number_integer()) {
				return std::to_string(value.get<long long>());
			}

			if (value.is_number_unsigned()) {
				return std::to_string(value.get<unsigned long long>());
			}

			if (value.is_number_float()) {
				std::ostringstream stream;
				stream << value.get<double>();
				return stream.str();
			}

			return std::nullopt;
		}

		void AppendFlagWithValue(
			std::vector<std::string>& args,
			const std::string& flag,
			const std::optional<std::string>& value) {
			if (!value.has_value() || value->empty()) {
				return;
			}

			args.push_back("--" + flag);
			args.push_back(value.value());
		}

		void AppendBoolAsValue(
			std::vector<std::string>& args,
			const std::string& flag,
			const nlohmann::json& params) {
			const auto it = params.find(flag);
			if (it == params.end() || !it->is_boolean()) {
				return;
			}

			args.push_back("--" + flag);
			args.push_back(it->get<bool>() ? "true" : "false");
		}

		std::optional<std::vector<std::string>> BuildImapSmtpCliArgs(
			const ImapSmtpToolRuntimeSpec& spec,
			const nlohmann::json& params,
			std::string& errorCode,
			std::string& errorMessage) {
			errorCode.clear();
			errorMessage.clear();

			std::vector<std::string> args;
			if (const auto accountIt = params.find("account");
				accountIt != params.end()) {
				AppendFlagWithValue(args, "account", JsonValueToCliString(*accountIt));
			}

			args.push_back(spec.command);

			if (spec.id == "imap_smtp_email.imap.fetch" ||
				spec.id == "imap_smtp_email.imap.download") {
				const auto uidIt = params.find("uid");
				if (uidIt == params.end()) {
					errorCode = "invalid_arguments";
					errorMessage = "uid is required";
					return std::nullopt;
				}

				const auto uid = JsonValueToCliString(*uidIt);
				if (!uid.has_value() || uid->empty()) {
					errorCode = "invalid_arguments";
					errorMessage = "uid is invalid";
					return std::nullopt;
				}

				args.push_back(uid.value());
			}

			if (spec.id == "imap_smtp_email.imap.mark_read" ||
				spec.id == "imap_smtp_email.imap.mark_unread") {
				const auto uidsIt = params.find("uids");
				if (uidsIt == params.end() || !uidsIt->is_array() || uidsIt->empty()) {
					errorCode = "invalid_arguments";
					errorMessage = "uids array is required";
					return std::nullopt;
				}

				for (const auto& uidNode : *uidsIt) {
					const auto uid = JsonValueToCliString(uidNode);
					if (uid.has_value() && !uid->empty()) {
						args.push_back(uid.value());
					}
				}
				if (args.back() == spec.command) {
					errorCode = "invalid_arguments";
					errorMessage = "uids array must contain at least one value";
					return std::nullopt;
				}
			}

			auto appendIfPresent = [&](const std::string& jsonKey, const std::string& cliFlag) {
				const auto it = params.find(jsonKey);
				if (it == params.end()) {
					return;
				}

				AppendFlagWithValue(args, cliFlag, JsonValueToCliString(*it));
				};

			if (spec.id == "imap_smtp_email.imap.check") {
				appendIfPresent("limit", "limit");
				appendIfPresent("mailbox", "mailbox");
				appendIfPresent("recent", "recent");
				AppendBoolAsValue(args, "unseen", params);
			}
			else if (spec.id == "imap_smtp_email.imap.download") {
				appendIfPresent("mailbox", "mailbox");
				appendIfPresent("dir", "dir");
				appendIfPresent("file", "file");
			}
			else if (spec.id == "imap_smtp_email.imap.search") {
				appendIfPresent("from", "from");
				appendIfPresent("subject", "subject");
				appendIfPresent("recent", "recent");
				appendIfPresent("since", "since");
				appendIfPresent("before", "before");
				appendIfPresent("limit", "limit");
				appendIfPresent("mailbox", "mailbox");
				AppendBoolAsValue(args, "unseen", params);
				AppendBoolAsValue(args, "seen", params);
			}
			else if (spec.id == "imap_smtp_email.imap.fetch" ||
				spec.id == "imap_smtp_email.imap.mark_read" ||
				spec.id == "imap_smtp_email.imap.mark_unread") {
				appendIfPresent("mailbox", "mailbox");
			}
			else if (spec.id == "imap_smtp_email.smtp.send") {
				appendIfPresent("to", "to");
				appendIfPresent("subject", "subject");
				appendIfPresent("subjectFile", "subject-file");
				appendIfPresent("body", "body");
				appendIfPresent("bodyFile", "body-file");
				appendIfPresent("htmlFile", "html-file");
				appendIfPresent("cc", "cc");
				appendIfPresent("bcc", "bcc");
				appendIfPresent("from", "from");

				const auto htmlIt = params.find("html");
				if (htmlIt != params.end() && htmlIt->is_boolean() && htmlIt->get<bool>()) {
					args.push_back("--html");
					args.push_back("true");
				}

				const auto attachIt = params.find("attach");
				if (attachIt != params.end()) {
					if (attachIt->is_array()) {
						std::string joined;
						for (const auto& item : *attachIt) {
							const auto value = JsonValueToCliString(item);
							if (!value.has_value() || value->empty()) {
								continue;
							}

							if (!joined.empty()) {
								joined += ",";
							}
							joined += value.value();
						}

						if (!joined.empty()) {
							args.push_back("--attach");
							args.push_back(joined);
						}
					}
					else {
						appendIfPresent("attach", "attach");
					}
				}

				const bool hasTo = params.contains("to");
				const bool hasSubject = params.contains("subject") || params.contains("subjectFile");
				if (!hasTo || !hasSubject) {
					errorCode = "invalid_arguments";
					errorMessage = "smtp.send requires to and subject or subjectFile";
					return std::nullopt;
				}
			}

			return args;
		}

		bool IsDateRangeTokenForBaiduSearch(const std::string& value) {
			if (value.size() != 22) {
				return false;
			}

			if (value[4] != '-' || value[7] != '-' || value[10] != 't' ||
				value[11] != 'o' || value[14] != '-' || value[17] != '-') {
				return false;
			}

			const std::size_t digitPositions[] = {
				0, 1, 2, 3, 5, 6, 8, 9, 12, 13, 15, 16, 18, 19, 20, 21
			};
			for (const auto pos : digitPositions) {
				if (!std::isdigit(static_cast<unsigned char>(value[pos]))) {
					return false;
				}
			}

			return true;
		}

		std::string ClassifyBaiduFailureCode(const std::string& output) {
			const std::string lowered = ToLowerAscii(output);
			if (lowered.find("baidu_api_key") != std::string::npos &&
				lowered.find("must be set") != std::string::npos) {
				return "baidu_api_key_missing";
			}

			if (lowered.find("http 401") != std::string::npos ||
				lowered.find("http 403") != std::string::npos) {
				return "auth_error";
			}

			if (lowered.find("http 429") != std::string::npos) {
				return "rate_limited";
			}

			if (lowered.find("timeout") != std::string::npos ||
				lowered.find("timed out") != std::string::npos) {
				return "network_timeout";
			}

			if (lowered.find("json parse error") != std::string::npos ||
				lowered.find("request body must be a json object") != std::string::npos ||
				lowered.find("query must be present") != std::string::npos ||
				lowered.find("freshness must be") != std::string::npos) {
				return "invalid_arguments";
			}

			if (lowered.find("network") != std::string::npos ||
				lowered.find("connection") != std::string::npos) {
				return "network_error";
			}

			return "process_exit_nonzero";
		}

		std::optional<std::vector<std::string>> BuildBaiduSearchCliArgs(
			const BaiduSearchToolRuntimeSpec& spec,
			const nlohmann::json& params,
			std::string& errorCode,
			std::string& errorMessage) {
			errorCode.clear();
			errorMessage.clear();

			if (spec.id != "baidu_search.search.web") {
				errorCode = "unsupported_tool";
				errorMessage = "unsupported baidu tool";
				return std::nullopt;
			}

			nlohmann::json cliPayload = nlohmann::json::object();
			const auto queryIt = params.find("query");
			if (queryIt == params.end() || !queryIt->is_string()) {
				errorCode = "invalid_arguments";
				errorMessage = "query is required";
				return std::nullopt;
			}

			const std::string query = TrimAsciiForBraveSearch(queryIt->get<std::string>());
			if (query.empty() || query.size() > 512 || HasControlCharsForBraveSearch(query)) {
				errorCode = "invalid_arguments";
				errorMessage = "query failed safety validation";
				return std::nullopt;
			}

			cliPayload["query"] = query;

			if (const auto countIt = params.find("count"); countIt != params.end()) {
				if (!countIt->is_number_integer()) {
					errorCode = "invalid_arguments";
					errorMessage = "count must be an integer";
					return std::nullopt;
				}

				const int count = countIt->get<int>();
				if (count < 1 || count > 50) {
					errorCode = "invalid_arguments";
					errorMessage = "count must be between 1 and 50";
					return std::nullopt;
				}

				cliPayload["count"] = count;
			}

			if (const auto freshnessIt = params.find("freshness"); freshnessIt != params.end()) {
				if (!freshnessIt->is_string()) {
					errorCode = "invalid_arguments";
					errorMessage = "freshness must be a string";
					return std::nullopt;
				}

				const std::string freshness = TrimAsciiForBraveSearch(
					freshnessIt->get<std::string>());
				if (freshness.empty()) {
					errorCode = "invalid_arguments";
					errorMessage = "freshness must be non-empty";
					return std::nullopt;
				}

				if (freshness != "pd" && freshness != "pw" && freshness != "pm" &&
					freshness != "py" && !IsDateRangeTokenForBaiduSearch(freshness)) {
					errorCode = "invalid_arguments";
					errorMessage =
						"freshness must be pd/pw/pm/py or YYYY-MM-DDtoYYYY-MM-DD";
					return std::nullopt;
				}

				cliPayload["freshness"] = freshness;
			}

			return std::vector<std::string>{ cliPayload.dump() };
		}

		std::optional<std::vector<std::string>> BuildBraveSearchCliArgs(
			const BraveSearchToolRuntimeSpec& spec,
			const nlohmann::json& params,
			std::string& errorCode,
			std::string& errorMessage) {
			errorCode.clear();
			errorMessage.clear();

			std::vector<std::string> args;
			if (spec.id == "brave_search.search.web") {
				const auto queryIt = params.find("query");
				if (queryIt == params.end() || !queryIt->is_string()) {
					errorCode = "invalid_arguments";
					errorMessage = "query is required";
					return std::nullopt;
				}

				const std::string query = TrimAsciiForBraveSearch(
					queryIt->get<std::string>());
				if (query.empty()) {
					errorCode = "invalid_arguments";
					errorMessage = "query is required";
					return std::nullopt;
				}

				if (query.size() > 512 || HasControlCharsForBraveSearch(query)) {
					errorCode = "invalid_arguments";
					errorMessage = "query failed safety validation";
					return std::nullopt;
				}

				args.push_back(query);

				int count = 0;
				if (const auto countIt = params.find("count");
					countIt != params.end() && countIt->is_number_integer()) {
					count = countIt->get<int>();
				}
				else if (const auto countIt = params.find("count");
					countIt != params.end()) {
					errorCode = "invalid_arguments";
					errorMessage = "count must be an integer";
					return std::nullopt;
				}
				else if (const auto topKIt = params.find("topK");
					topKIt != params.end() && topKIt->is_number_integer()) {
					count = topKIt->get<int>();
				}
				else if (const auto topKIt = params.find("topK");
					topKIt != params.end()) {
					errorCode = "invalid_arguments";
					errorMessage = "topK must be an integer";
					return std::nullopt;
				}

				if (count > 0) {
					if (count < 1 || count > 20) {
						errorCode = "invalid_arguments";
						errorMessage = "count must be between 1 and 20";
						return std::nullopt;
					}

					args.push_back("-n");
					args.push_back(std::to_string(count));
				}

				if (const auto contentIt = params.find("content");
					contentIt != params.end() && contentIt->is_boolean() &&
					contentIt->get<bool>()) {
					args.push_back("--content");
				}
				else if (const auto contentIt = params.find("content");
					contentIt != params.end() && !contentIt->is_boolean()) {
					errorCode = "invalid_arguments";
					errorMessage = "content must be a boolean";
					return std::nullopt;
				}
			}
			else if (spec.id == "brave_search.fetch.content") {
				const auto urlIt = params.find("url");
				if (urlIt == params.end() || !urlIt->is_string()) {
					errorCode = "invalid_arguments";
					errorMessage = "url is required";
					return std::nullopt;
				}

				const std::string url = TrimAsciiForBraveSearch(urlIt->get<std::string>());
				if (url.empty()) {
					errorCode = "invalid_arguments";
					errorMessage = "url is required";
					return std::nullopt;
				}

				if (url.size() > 2048 ||
					HasControlCharsForBraveSearch(url) ||
					!IsHttpUrlForBraveSearch(url)) {
					errorCode = "invalid_arguments";
					errorMessage = "url failed safety validation";
					return std::nullopt;
				}

				args.push_back(url);
			}

			return args;
		}

		void RegisterBaiduSearchRuntimeTools(blazeclaw::gateway::GatewayHost& host) {
			const auto skillRoot = ResolveBaiduSearchSkillRoot();
			for (const auto& spec : BuildBaiduSearchToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "search",
						.enabled = true,
					},
					[spec, skillRoot](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "baidu-search skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							params = nlohmann::json::object(
								{ {"query", params.get<std::string>()} });
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = BuildBaiduSearchCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs = 45000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = ExecutePythonSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = process.output;
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						const std::string classifiedFailure =
							ClassifyBaiduFailureCode(process.output);
						result.status = classifiedFailure;
						result.errorCode = classifiedFailure;
						if (!result.result.empty()) {
							result.errorMessage = result.result;
						}
						else {
							result.errorMessage =
								"tool process returned non-zero exit code " +
								std::to_string(static_cast<unsigned long long>(process.exitCode));
						}
						return result;
					});
			}
		}

		void RegisterImapSmtpRuntimeTools(blazeclaw::gateway::GatewayHost& host) {
			const auto skillRoot = ResolveImapSmtpSkillRoot();
			for (const auto& spec : BuildImapSmtpToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "email",
						.enabled = true,
					},
					[spec, skillRoot](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "imap-smtp-email skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = BuildImapSmtpCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs = 120000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = ExecuteNodeSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = process.output;
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						result.status = "error";
						result.errorCode = "process_exit_nonzero";
						result.errorMessage =
							"tool process returned non-zero exit code " +
							std::to_string(static_cast<unsigned long long>(process.exitCode));
						return result;
					});
			}
		}

		void RegisterBraveSearchRuntimeTools(blazeclaw::gateway::GatewayHost& host) {
			const auto skillRoot = ResolveBraveSearchSkillRoot();
			const bool requireApiKey = ResolveBraveRequireApiKey();
			const bool hasApiKey = HasEnvVarValue(L"BRAVE_API_KEY");
			for (const auto& spec : BuildBraveSearchToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "search",
						.enabled = true,
					},
					[spec, skillRoot, requireApiKey, hasApiKey](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (requireApiKey && !hasApiKey) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "brave_api_key_missing";
							result.errorMessage =
								"BRAVE_API_KEY is required by runtime policy";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "brave-search skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							if (spec.id == "brave_search.search.web") {
								params = nlohmann::json::object(
									{ {"query", params.get<std::string>()} });
							}
							else if (spec.id == "brave_search.fetch.content") {
								params = nlohmann::json::object(
									{ {"url", params.get<std::string>()} });
							}
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = BuildBraveSearchCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs =
							spec.id == "brave_search.search.web" ? 45000 : 30000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = ExecuteNodeSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = TruncateBraveToolOutput(process.output);
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = TruncateBraveToolOutput(process.output);
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = TruncateBraveToolOutput(process.output);
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						const std::string classifiedFailure =
							ClassifyBraveFailureCode(process.output);
						result.status = classifiedFailure;
						result.errorCode = classifiedFailure;
						if (!result.result.empty()) {
							result.errorMessage = result.result;
						}
						else {
							result.errorMessage =
								"tool process returned non-zero exit code " +
								std::to_string(static_cast<unsigned long long>(process.exitCode));
						}
						return result;
					});
			}
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
			const std::wstring& skillsPrompt,
			const std::size_t maxPromptChars) {
			if (skillsPrompt.empty()) {
				return userMessage;
			}

			std::string narrowedPrompt = WideToNarrowAscii(skillsPrompt);
			if (narrowedPrompt.empty()) {
				return userMessage;
			}

			if (maxPromptChars > 0 && narrowedPrompt.size() > maxPromptChars) {
				narrowedPrompt.resize(maxPromptChars);
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

	ServiceManager::EmailFallbackResolvedPolicy
		ServiceManager::ResolveEmailFallbackPolicy(
			const std::wstring& toolName,
			const std::wstring& capabilityName) const {
		const auto resolvedPolicy = blazeclaw::config::ResolveEmailFallbackPolicy(
			m_activeConfig.email.policy,
			toolName,
			capabilityName);

		EmailFallbackResolvedPolicy resolved;
		resolved.profileId = resolvedPolicy.profileId;
		resolved.backends = resolvedPolicy.backends;
		resolved.onUnavailable = resolvedPolicy.onUnavailable;
		resolved.onAuthError = resolvedPolicy.onAuthError;
		resolved.onExecError = resolvedPolicy.onExecError;
		resolved.retryMaxAttempts = resolvedPolicy.retryMaxAttempts;
		resolved.retryDelayMs = resolvedPolicy.retryDelayMs;
		resolved.requiresApproval = resolvedPolicy.requiresApproval;
		resolved.approvalTokenTtlMinutes = resolvedPolicy.approvalTokenTtlMinutes;

		return resolved;
	}

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
			const auto commandIt = commandsBySkill.find(entry.skillName);
			const auto installIt = installBySkill.find(entry.skillName);

			gatewaySkillsState.entries.push_back(BuildGatewaySkillEntry(
				entry,
				eligibilityIt != eligibilityByName.end() ? &eligibilityIt->second : nullptr,
				commandIt != commandsBySkill.end() ? &commandIt->second : nullptr,
				installIt != installBySkill.end() ? &installIt->second : nullptr));
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

	blazeclaw::gateway::SkillsCatalogGatewayEntry
		ServiceManager::BuildGatewaySkillEntry(
			const SkillsCatalogEntry& entry,
			const SkillsEligibilityEntry* eligibility,
			const SkillsCommandSpec* command,
			const SkillsInstallPlanEntry* install) const {
		std::vector<std::string> metadataSources;

		std::string commandName;
		std::string commandToolName;
		std::string commandArgMode;
		std::string commandArgSchema;
		std::string commandResultSchema;
		std::string commandIdempotencyHint;
		std::string commandRetryPolicyHint;
		bool commandRequiresApproval = false;
		if (command != nullptr) {
			commandName = ToNarrow(command->name);
			commandToolName = ToNarrow(command->dispatch.toolName);
			commandArgMode = ToNarrow(command->dispatch.argMode);
			commandArgSchema = ToNarrow(command->dispatch.argSchema);
			commandResultSchema = ToNarrow(command->dispatch.resultSchema);
			commandIdempotencyHint = ToNarrow(command->dispatch.idempotencyHint);
			commandRetryPolicyHint = ToNarrow(command->dispatch.retryPolicyHint);
			commandRequiresApproval = command->dispatch.requiresApproval;
		}

		std::string installKind;
		std::string installCommand;
		std::string installReason;
		bool installExecutable = false;
		if (install != nullptr) {
			installKind = ToNarrow(install->kind);
			installCommand = ToNarrow(install->command);
			installReason = ToNarrow(install->reason);
			installExecutable = install->executable;
		}

		std::string primaryEnv;
		std::vector<std::string> requiresBins;
		std::vector<std::string> requiresEnv;
		std::vector<std::string> requiresConfig;

		if (const auto* value = ResolveNormalizedField(
			entry.frontmatter,
			{ L"metadata.blazeclaw.primaryenv", L"metadata.blazeclaw.primary_env" },
			{ L"metadata.openclaw.primaryenv", L"metadata.openclaw.primary_env" },
			metadataSources);
			value != nullptr) {
			primaryEnv = ToNarrow(*value);
		}

		if (const auto* value = ResolveNormalizedField(
			entry.frontmatter,
			{ L"metadata.blazeclaw.requires.bins" },
			{ L"metadata.openclaw.requires.bins" },
			metadataSources);
			value != nullptr) {
			requiresBins = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
		}

		if (const auto* value = ResolveNormalizedField(
			entry.frontmatter,
			{ L"metadata.blazeclaw.requires.env" },
			{ L"metadata.openclaw.requires.env" },
			metadataSources);
			value != nullptr) {
			requiresEnv = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
		}

		if (const auto* value = ResolveNormalizedField(
			entry.frontmatter,
			{ L"metadata.blazeclaw.requires.config" },
			{ L"metadata.openclaw.requires.config" },
			metadataSources);
			value != nullptr) {
			requiresConfig = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
		}

		std::vector<std::string> configPathHints;
		for (const auto& configKey : requiresConfig) {
			if (configKey == "channels.discord.token") {
				configPathHints.push_back("credentials.discord.token");
			}
			else if (configKey == "channels.slack") {
				configPathHints.push_back("channels.slack.default");
			}
			else if (configKey == "plugins.entries.voice-call.enabled") {
				configPathHints.push_back("plugins.voice-call.enabled");
			}
			else {
				configPathHints.push_back(configKey);
			}

			if (std::find(
				configPathHints.begin(),
				configPathHints.end(),
				configKey) == configPathHints.end()) {
				configPathHints.push_back(configKey);
			}
		}

		blazeclaw::gateway::SkillsCatalogGatewayEntry gatewayEntry;
		gatewayEntry.name = ToNarrow(entry.skillName);
		gatewayEntry.skillKey = eligibility != nullptr
			? ToNarrow(eligibility->skillKey)
			: ToNarrow(entry.skillName);
		gatewayEntry.commandName = commandName;
		gatewayEntry.commandToolName = commandToolName;
		gatewayEntry.commandArgMode = commandArgMode;
		gatewayEntry.commandArgSchema = commandArgSchema;
		gatewayEntry.commandResultSchema = commandResultSchema;
		gatewayEntry.commandIdempotencyHint = commandIdempotencyHint;
		gatewayEntry.commandRetryPolicyHint = commandRetryPolicyHint;
		gatewayEntry.commandRequiresApproval = commandRequiresApproval;
		gatewayEntry.installKind = installKind;
		gatewayEntry.installCommand = installCommand;
		gatewayEntry.installExecutable = installExecutable;
		gatewayEntry.installReason = installReason;
		gatewayEntry.description = ToNarrow(entry.description);
		gatewayEntry.source = ToNarrow(
			SkillsCatalogService::SourceKindLabel(entry.sourceKind));
		gatewayEntry.precedence = entry.precedence;
		gatewayEntry.eligible = eligibility != nullptr ? eligibility->eligible : false;
		gatewayEntry.disabled = eligibility != nullptr ? eligibility->disabled : false;
		gatewayEntry.blockedByAllowlist = eligibility != nullptr
			? eligibility->blockedByAllowlist
			: false;
		gatewayEntry.disableModelInvocation = eligibility != nullptr
			? eligibility->disableModelInvocation
			: false;
		gatewayEntry.validFrontmatter = entry.validFrontmatter;
		gatewayEntry.validationErrorCount = entry.validationErrors.size();
		gatewayEntry.primaryEnv = primaryEnv;
		gatewayEntry.requiresBins = std::move(requiresBins);
		gatewayEntry.requiresEnv = std::move(requiresEnv);
		gatewayEntry.requiresConfig = std::move(requiresConfig);
		gatewayEntry.configPathHints = std::move(configPathHints);
		gatewayEntry.normalizedMetadataSources = std::move(metadataSources);
		if (eligibility != nullptr) {
			gatewayEntry.missingEnv = UniqueNarrowValues(eligibility->missingEnv);
			gatewayEntry.missingConfig = UniqueNarrowValues(eligibility->missingConfig);
			gatewayEntry.missingBins = UniqueNarrowValues(eligibility->missingBins);
			gatewayEntry.missingAnyBins =
				UniqueNarrowValues(eligibility->missingAnyBins);
		}
		return gatewayEntry;
	}

	void ServiceManager::RefreshSkillsState(
		const blazeclaw::config::AppConfig& config,
		const bool forceRefresh,
		const std::wstring& reason) {
		const auto workspaceRoot =
			ResolveWorkspaceRootForSkills(std::filesystem::current_path());
		m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
			workspaceRoot,
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
			workspaceRoot,
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
		AppendStartupTrace("ServiceManager.Start.begin");

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
		m_emailFallbackResolvedPolicy = ResolveEmailFallbackPolicy(
			L"email.schedule",
			L"email.send");
		m_emailPolicyRolloutMode =
			m_activeConfig.email.policyProfiles.rolloutMode;
		if (m_emailPolicyRolloutMode.empty()) {
			m_emailPolicyRolloutMode = L"legacy";
		}
		m_emailPolicyEnforceChannel =
			m_activeConfig.email.policyProfiles.enforceChannel;
		m_emailPolicyRollbackBridgeEnabled =
			m_activeConfig.email.policyProfiles.rollbackBridgeEnabled;
		m_emailPolicyCanaryEligible =
			m_emailPolicyEnforceChannel.empty() ||
			IsOneOfChannels(
				m_activeConfig.enabledChannels,
				m_emailPolicyEnforceChannel);
		m_emailPolicyRuntimeEnabled =
			m_activeConfig.email.policyProfiles.enabled;
		m_emailPolicyRuntimeEnforce =
			m_activeConfig.email.policyProfiles.enforce;
		if (_wcsicmp(m_emailPolicyRolloutMode.c_str(), L"monitor") == 0) {
			m_emailPolicyRuntimeEnabled = true;
			m_emailPolicyRuntimeEnforce = false;
		}
		else if (_wcsicmp(m_emailPolicyRolloutMode.c_str(), L"enforce") == 0) {
			m_emailPolicyRuntimeEnabled = true;
			m_emailPolicyRuntimeEnforce = m_emailPolicyCanaryEligible;
		}
		else {
			m_emailPolicyRuntimeEnabled =
				m_activeConfig.email.policyProfiles.enabled;
			m_emailPolicyRuntimeEnforce =
				m_activeConfig.email.policyProfiles.enforce;
		}

		if (!m_emailPolicyRollbackBridgeEnabled) {
			m_emailPolicyRuntimeEnabled =
				m_activeConfig.email.policyProfiles.enabled;
			m_emailPolicyRuntimeEnforce =
				m_activeConfig.email.policyProfiles.enforce;
		}

		if (m_emailPolicyRuntimeEnabled &&
			!m_activeConfig.email.policyProfiles.enabled) {
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"email policy rollout gate activated runtime policy profile monitor/enforce mode.");
		}
		AppendStartupTrace("ServiceManager.Start.policy.ready");
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
		AppendStartupTrace("ServiceManager.Start.agents.ready");
		m_embeddingsService.Configure(m_activeConfig);
		m_embeddings = m_embeddingsService.Snapshot();
		AppendStartupTrace("ServiceManager.Start.embeddings.ready");

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
		const bool localModelStartupLoadEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_LOCALMODEL_STARTUP_LOAD_ENABLED",
			false);
		AppendStartupTrace("ServiceManager.Start.localmodel.beforeLoad");
		bool localModelLoaded = false;
		if (m_activeConfig.localModel.enabled &&
			m_localModelRolloutEligible &&
			localModelStartupLoadEnabled) {
			localModelLoaded = m_localModelRuntime.LoadModel();
		}
		else if (m_activeConfig.localModel.enabled &&
			m_localModelRolloutEligible) {
			m_localModelActivationReason = "startup_load_deferred";
		}
		m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
		AppendStartupTrace("ServiceManager.Start.localmodel.afterLoad");
		if (!localModelLoaded && m_localModelRuntimeSnapshot.status.empty()) {
			m_localModelRuntimeSnapshot.status = localModelStartupLoadEnabled
				? "load_failed"
				: "startup_load_deferred";
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
			m_localModelActivationReason = localModelStartupLoadEnabled
				? "initialization_failed"
				: "startup_load_deferred";
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
		AppendStartupTrace("ServiceManager.Start.retrieval.ready");
		m_embeddedDynamicLoopCanaryProviders = ParseCsvEnvValues(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_PROVIDERS");
		m_embeddedDynamicLoopCanarySessions = ParseCsvEnvValues(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_CANARY_SESSIONS");
		m_embeddedDynamicLoopPromotionMinRuns = ParseUInt64EnvValue(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_RUNS",
			20);
		m_embeddedDynamicLoopPromotionMinSuccessRate = ParseDoubleEnvValue(
			L"BLAZECLAW_EMBEDDED_DYNAMIC_LOOP_PROMOTION_MIN_SUCCESS_RATE",
			0.95);
		m_embeddedRunSuccessCount = 0;
		m_embeddedRunFailureCount = 0;
		m_embeddedRunTimeoutCount = 0;
		m_embeddedRunCancelledCount = 0;
		m_embeddedRunFallbackCount = 0;
		m_embeddedTaskDeltaTransitionCount = 0;
		m_emailFallbackAttemptCount = 0;
		m_emailFallbackSuccessCount = 0;
		m_emailFallbackFailureCount = 0;
		m_lastEmbeddedDynamicLoopEnabled = false;
		m_lastEmbeddedCanaryEligible = false;
		m_lastEmbeddedPromotionReady = false;
		m_lastEmbeddedFallbackUsed = false;
		m_lastEmbeddedFallbackReason.clear();
		m_chatRuntimeAsyncQueueEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_CHAT_RUNTIME_ASYNC_QUEUE_ENABLED",
			true);
		m_chatRuntimeQueueWaitTimeoutMs = ParseUInt64EnvValue(
			L"BLAZECLAW_CHAT_RUNTIME_QUEUE_WAIT_TIMEOUT_MS",
			kChatRuntimeQueueWaitTimeoutMs);
		m_chatRuntimeExecutionTimeoutMs = ParseUInt64EnvValue(
			L"BLAZECLAW_CHAT_RUNTIME_EXECUTION_TIMEOUT_MS",
			kChatRuntimeExecutionTimeoutMs);
		{
			std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
			m_chatRuntimeQueue.clear();
			m_chatRuntimeJobsByRunId.clear();
			m_chatRuntimeRunsById.clear();
			m_chatRuntimeNextEnqueueSequence = 1;
			m_chatRuntimeWorkerStopRequested = false;
			m_chatRuntimeWorkerAvailable = false;
		}
		if (m_chatRuntimeAsyncQueueEnabled) {
			const bool chatRuntimeWorkerStarted = StartChatRuntimeWorker();
			if (!chatRuntimeWorkerStarted) {
				TRACE(
					"[ChatRuntime] worker unavailable; callbacks will return %s\n",
					kChatRuntimeErrorWorkerUnavailable);
			}
		}
		else {
			TRACE(
				"[ChatRuntime] async queue disabled by rollout flag; using synchronous runtime path\n");
		}
		const bool startupSkillsRefreshEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_SKILLS_STARTUP_REFRESH_ENABLED",
			false);
		if (startupSkillsRefreshEnabled) {
			RefreshSkillsState(m_activeConfig, true, L"startup");
			AppendStartupTrace("ServiceManager.Start.skills.refreshed");
		}
		else {
			const auto workspaceRoot =
				ResolveWorkspaceRootForSkills(std::filesystem::current_path());
			m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
				workspaceRoot,
				m_activeConfig);
			m_skillsEligibility = m_skillsEligibilityService.Evaluate(
				m_skillsCatalog,
				m_activeConfig);
			m_hookCatalog = m_hookCatalogService.BuildSnapshot(m_skillsCatalog);
			m_hookExecution = m_hookExecutionService.Snapshot();
			m_skillsPrompt = m_skillsPromptService.BuildSnapshot(
				m_skillsCatalog,
				m_skillsEligibility,
				m_activeConfig,
				std::nullopt,
				m_hooksFallbackPromptInjection);
			m_hookEvents = m_hookEventService.Snapshot();
			m_skillsCommands = m_skillsCommandService.BuildSnapshot(
				m_skillsCatalog,
				m_skillsEligibility);
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"skills startup full refresh skipped; minimal startup catalog loaded.");
			AppendStartupTrace("ServiceManager.Start.skills.refresh.minimal");
		}

		const bool startupHookBootstrapEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_HOOKS_STARTUP_BOOTSTRAP_ENABLED",
			false);
		if (startupSkillsRefreshEnabled && startupHookBootstrapEnabled) {
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
		}
		else {
			AppendStartupTrace("ServiceManager.Start.hooks.bootstrap.skipped");
		}

		std::wstring fixtureError;
		const std::vector<std::filesystem::path> fixtureCandidates = {
			std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
			std::filesystem::current_path() / L"fixtures" / L"agents",
			std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
			std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
		};

		const bool startupFixtureValidationEnabled = ReadBoolEnvOrDefault(
			L"BLAZECLAW_FIXTURES_STARTUP_VALIDATION_ENABLED",
			false);
		if (startupFixtureValidationEnabled) {
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
		}
		else {
			AppendStartupTrace("ServiceManager.Start.fixtures.validation.skipped");
		}

		m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
		m_gatewayHost.SetSkillsRefreshCallback([this]() {
			RefreshSkillsState(m_activeConfig, true, L"manual-refresh");
			return BuildGatewaySkillsState();
			});
		m_gatewayHost.SetSkillsUpdateCallback([this](
			const blazeclaw::gateway::protocol::RequestFrame& request) {
				std::string skill;
				if (!request.paramsJson.has_value() ||
					!blazeclaw::gateway::json::FindStringField(
						request.paramsJson.value(),
						"skill",
						skill) ||
					blazeclaw::gateway::json::Trim(skill).empty()) {
					return blazeclaw::gateway::protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = blazeclaw::gateway::protocol::ErrorShape{
							.code = "missing_skill",
							.message = "Parameter `skill` is required.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				std::string apiKey;
				blazeclaw::gateway::json::FindStringField(
					request.paramsJson.value(),
					"apiKey",
					apiKey);

				std::string envRaw;
				blazeclaw::gateway::json::FindRawField(
					request.paramsJson.value(),
					"env",
					envRaw);

				std::string configKey;
				std::string configValue;
				blazeclaw::gateway::json::FindStringField(
					request.paramsJson.value(),
					"configKey",
					configKey);
				blazeclaw::gateway::json::FindStringField(
					request.paramsJson.value(),
					"configValue",
					configValue);

				std::ostringstream envOut;
				if (!apiKey.empty()) {
					envOut << "API_KEY=" << apiKey << "\n";
				}

				if (!envRaw.empty()) {
					std::string compactEnv = blazeclaw::gateway::json::Trim(envRaw);
					if (!compactEnv.empty() && compactEnv.front() == '{' && compactEnv.back() == '}') {
						nlohmann::json envObj;
						try {
							envObj = nlohmann::json::parse(compactEnv);
						}
						catch (...) {
							return blazeclaw::gateway::protocol::ResponseFrame{
								.id = request.id,
								.ok = false,
								.payloadJson = std::nullopt,
								.error = blazeclaw::gateway::protocol::ErrorShape{
									.code = "invalid_env_payload",
									.message = "Parameter `env` must be a valid JSON object.",
									.detailsJson = std::nullopt,
									.retryable = false,
									.retryAfterMs = std::nullopt,
								},
							};
						}

						if (envObj.is_object()) {
							for (auto it = envObj.begin(); it != envObj.end(); ++it) {
								if (it.value().is_string()) {
									envOut << it.key() << "=" << it.value().get<std::string>() << "\n";
								}
							}
						}
					}
				}

				if (!configKey.empty()) {
					envOut << configKey << "=" << configValue << "\n";
				}

				const std::string envContent = envOut.str();
				if (envContent.empty()) {
					return blazeclaw::gateway::protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = blazeclaw::gateway::protocol::ErrorShape{
							.code = "empty_update",
							.message = "No update payload was provided.",
							.detailsJson = std::nullopt,
						  .retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				CBlazeClawMFCDoc* activeDoc = nullptr;
				auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
				if (mainFrame != nullptr) {
					auto* activeChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, mainFrame->MDIGetActive());
					if (activeChild != nullptr) {
						auto* activeView = DYNAMIC_DOWNCAST(CBlazeClawMFCView, activeChild->GetActiveView());
						if (activeView != nullptr) {
							activeDoc = activeView->GetDocument();
						}
					}
				}

				if (activeDoc == nullptr) {
					return blazeclaw::gateway::protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = blazeclaw::gateway::protocol::ErrorShape{
							.code = "doc_unavailable",
							.message = "No active document context for skill update.",
							.detailsJson = std::nullopt,
						   .retryable = true,
							.retryAfterMs = 100,
						},
					};
				}

				std::string persistError;
				std::filesystem::path savedPath;
				if (!activeDoc->SaveSkillConfigEnv(skill, envContent, persistError, &savedPath)) {
					return blazeclaw::gateway::protocol::ResponseFrame{
						.id = request.id,
						.ok = false,
						.payloadJson = std::nullopt,
						.error = blazeclaw::gateway::protocol::ErrorShape{
							.code = "persist_failed",
							.message = persistError.empty()
								? "Failed to persist skill update payload."
								: persistError,
							.detailsJson = std::nullopt,
						  .retryable = false,
							.retryAfterMs = std::nullopt,
						},
					};
				}

				RefreshSkillsState(m_activeConfig, true, L"skills.update");
				m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
				if (mainFrame != nullptr) {
					mainFrame->RefreshSkillView();
				}

				const std::string payload =
					"{\"skill\":\"" + EscapeJsonUtf8(skill) +
					"\",\"configPath\":\"" + EscapeJsonUtf8(ToNarrow(savedPath.wstring())) +
					"\",\"updated\":true}";

				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = true,
					.payloadJson = payload,
					.error = std::nullopt,
				};
			});
		m_gatewayHost.SetEmbeddedOrchestrationPath(
			ToNarrow(m_activeConfig.embedded.orchestrationPath));
		m_gatewayHost.SetEmailFallbackRuntimeFlags(
			m_activeConfig.email.preflight.enabled,
			m_emailPolicyRuntimeEnabled,
			m_emailPolicyRuntimeEnforce);
		std::vector<std::string> resolvedBackends;
		resolvedBackends.reserve(m_emailFallbackResolvedPolicy.backends.size());
		for (const auto& backend : m_emailFallbackResolvedPolicy.backends) {
			resolvedBackends.push_back(ToNarrow(backend));
		}
		m_gatewayHost.SetEmailFallbackResolvedPolicy(
			resolvedBackends,
			ToNarrow(m_emailFallbackResolvedPolicy.onUnavailable),
			ToNarrow(m_emailFallbackResolvedPolicy.onAuthError),
			ToNarrow(m_emailFallbackResolvedPolicy.onExecError),
			m_emailPolicyRuntimeEnabled
			? m_emailFallbackResolvedPolicy.retryMaxAttempts
			: std::uint32_t{ 1 },
			m_emailPolicyRuntimeEnabled
			? m_emailFallbackResolvedPolicy.retryDelayMs
			: std::uint32_t{ 0 },
			m_emailFallbackResolvedPolicy.requiresApproval,
			m_emailFallbackResolvedPolicy.approvalTokenTtlMinutes,
			m_emailPolicyRuntimeEnabled
			? ToNarrow(m_emailFallbackResolvedPolicy.profileId)
			: std::string("legacy-policy"));
		RegisterImapSmtpRuntimeTools(m_gatewayHost);
		RegisterBraveSearchRuntimeTools(m_gatewayHost);
		RegisterBaiduSearchRuntimeTools(m_gatewayHost);

		m_gatewayHost.SetChatRuntimeCallback([this](
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
				const std::string sessionId =
					request.sessionKey.empty() ? "main" : request.sessionKey;
				const std::string runtimeMessage =
					BuildSkillsInjectedMessage(
						request.message,
						m_skillsPrompt.prompt,
						static_cast<std::size_t>(
							m_activeConfig.skills.limits.maxSkillsPromptChars));
				const std::string activeProvider = m_activeChatProvider;
				const std::string activeModel = m_activeChatModel;
				auto executeRequest = [this,
					request,
					sessionId,
					runtimeMessage,
					activeProvider,
					activeModel]() -> blazeclaw::gateway::GatewayHost::ChatRuntimeResult {
					if (IsEmbeddedRunCancelled(request.runId) ||
						IsDeepSeekRunCancelled(request.runId)) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = activeModel,
							.errorCode = kChatRuntimeErrorCancelled,
							.errorMessage = "chat runtime cancelled",
						};
					}

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
						 .argMode = WideToNarrowAscii(command.dispatch.argMode),
							});
					}

					const bool canaryEligible = IsEmbeddedDynamicLoopCanaryEligible(
						activeProvider,
						sessionId);
					const bool promotionReady = IsEmbeddedDynamicLoopPromotionReady();
					const bool enableEmbeddedDynamicLoop =
						m_activeConfig.embedded.dynamicToolLoopEnabled &&
						(canaryEligible || promotionReady);
					m_lastEmbeddedDynamicLoopEnabled = enableEmbeddedDynamicLoop;
					m_lastEmbeddedCanaryEligible = canaryEligible;
					m_lastEmbeddedPromotionReady = promotionReady;
					m_lastEmbeddedFallbackUsed = false;
					m_lastEmbeddedFallbackReason.clear();

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
						   .enableDynamicToolLoop = enableEmbeddedDynamicLoop,
						 .toolExecutorV2 = [this](
								const blazeclaw::gateway::ToolExecuteRequestV2& executeRequest) {
								return m_gatewayHost.ExecuteRuntimeToolV2(executeRequest);
							},
							.toolExecutor = [this](
												const std::string& tool,
												const std::optional<std::string>& argsJson) {
							  return m_gatewayHost.ExecuteRuntimeTool(tool, argsJson);
							},
						 .isCancellationRequested = [this, runId = request.runId]() {
								return IsEmbeddedRunCancelled(runId);
							},
						});
					ClearEmbeddedRunCancelled(request.runId);

					if (!embeddedExecution.accepted) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
						   .modelId = activeModel,
							.errorCode = "embedded_run_rejected",
							.errorMessage = embeddedExecution.errorMessage.empty()
								? embeddedExecution.reason
								: embeddedExecution.errorMessage,
						};
					}

					if (embeddedExecution.handled) {
						std::vector<blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
							runtimeDeltas;
						runtimeDeltas.reserve(embeddedExecution.taskDeltas.size());
						for (const auto& delta : embeddedExecution.taskDeltas) {
							runtimeDeltas.push_back(
								blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
									.index = delta.index,
									.runId = delta.runId,
									.sessionId = delta.sessionId,
									.phase = delta.phase,
									.toolName = delta.toolName,
								 .fallbackBackend = delta.fallbackBackend,
									.fallbackAction = delta.fallbackAction,
									.fallbackAttempt = delta.fallbackAttempt,
									.fallbackMaxAttempts = delta.fallbackMaxAttempts,
									.argsJson = delta.argsJson,
									.resultJson = delta.resultJson,
									.status = delta.status,
									.errorCode = delta.errorCode,
									.startedAtMs = delta.startedAtMs,
									.completedAtMs = delta.completedAtMs,
									.latencyMs = delta.latencyMs,
									.modelTurnId = delta.modelTurnId,
									.stepLabel = delta.stepLabel,
								});
						}

						m_embeddedTaskDeltaTransitionCount +=
							static_cast<std::uint64_t>(embeddedExecution.taskDeltas.size());
						if (embeddedExecution.success) {
							++m_embeddedRunSuccessCount;
						}
						else {
							++m_embeddedRunFailureCount;
						}

						const std::string normalizedEmbeddedError =
							ToLowerAscii(embeddedExecution.errorCode);
						if (normalizedEmbeddedError == "embedded_deadline_exceeded") {
							++m_embeddedRunTimeoutCount;
						}

						if (normalizedEmbeddedError == "embedded_run_cancelled") {
							++m_embeddedRunCancelledCount;
						}

						if (!embeddedExecution.success) {
							if (ShouldFallbackFromEmbeddedFailure(
								embeddedExecution.errorCode,
								embeddedExecution.reason)) {
								m_lastEmbeddedFallbackUsed = true;
								++m_embeddedRunFallbackCount;
								m_lastEmbeddedFallbackReason = embeddedExecution.errorCode.empty()
									? embeddedExecution.reason
									: embeddedExecution.errorCode;
							}
							else {
								return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
									.ok = false,
									.assistantText = {},
									.assistantDeltas = embeddedExecution.assistantDeltas,
									.taskDeltas = std::move(runtimeDeltas),
								   .modelId = activeModel,
									.errorCode = embeddedExecution.errorCode.empty()
										? "embedded_tool_execution_failed"
										: embeddedExecution.errorCode,
									.errorMessage = embeddedExecution.errorMessage.empty()
										? "embedded tool orchestration failed"
										: embeddedExecution.errorMessage,
								};
							}
						}

						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = true,
							.assistantText = embeddedExecution.assistantText,
							.assistantDeltas = embeddedExecution.assistantDeltas,
						   .taskDeltas = std::move(runtimeDeltas),
						   .modelId = activeModel,
							.errorCode = {},
							.errorMessage = {},
						};
					}

					auto providerRequest = request;
					providerRequest.message = runtimeMessage;

					if (activeProvider == "deepseek") {
						ClearDeepSeekRunCancelled(providerRequest.runId);
						if (!HasDeepSeekCredential()) {
							return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
								.ok = false,
								.assistantText = {},
							   .modelId = activeModel,
								.errorCode = "deepseek_api_key_missing",
								.errorMessage =
									"DeepSeek API key missing. Configure DeepSeek extension first.",
							};
						}

						const std::string effectiveModel = NormalizeDeepSeekApiModelId(
							activeModel.empty() ? "deepseek-chat" : activeModel);
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
					};

				if (!m_chatRuntimeAsyncQueueEnabled) {
					return executeRequest();
				}

				const std::uint64_t enqueuedAtMs = CurrentEpochMs();
				auto job = std::make_shared<ChatRuntimeJob>();
				{
					std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
					if (!m_chatRuntimeWorkerAvailable) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = activeModel,
							.errorCode = kChatRuntimeErrorWorkerUnavailable,
							.errorMessage = "chat runtime worker unavailable",
						};
					}

					if (m_chatRuntimeQueue.size() >= kChatRuntimeQueueCapacity) {
						return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
							.ok = false,
							.assistantText = {},
							.modelId = activeModel,
							.errorCode = kChatRuntimeErrorQueueFull,
							.errorMessage = "chat runtime queue capacity reached",
						};
					}

					job->enqueueSequence = m_chatRuntimeNextEnqueueSequence++;
					job->enqueuedAtMs = enqueuedAtMs;
					job->status = ChatRuntimeJobLifecycleStatus::Queued;
					job->request = request;
					job->sessionId = sessionId;
					job->runtimeMessage = runtimeMessage;
					job->provider = activeProvider;
					job->model = activeModel;
					job->execute = std::move(executeRequest);

					m_chatRuntimeQueue.push_back(job);
					m_chatRuntimeJobsByRunId.insert_or_assign(request.runId, job);
					m_chatRuntimeRunsById[request.runId] = ChatRuntimeRunState{
						.runId = request.runId,
						.sessionId = sessionId,
						.provider = activeProvider,
						.model = activeModel,
						.enqueuedAtMs = enqueuedAtMs,
						.startedAtMs = 0,
						.completedAtMs = 0,
						.status = ChatRuntimeJobLifecycleStatus::Queued,
						.errorCode = {},
					};
				}

				m_chatRuntimeQueueCv.notify_one();

				std::unique_lock<std::mutex> completionLock(job->completionMutex);
				const auto waitBudget =
					std::chrono::milliseconds(
						m_chatRuntimeQueueWaitTimeoutMs +
						m_chatRuntimeExecutionTimeoutMs +
						1000);
				if (!job->completionCv.wait_for(completionLock, waitBudget, [job]() {
					return job->completed;
					})) {
					MarkEmbeddedRunCancelled(request.runId);
					MarkDeepSeekRunCancelled(request.runId);
					const bool localCancelRequested =
						m_localModelRuntime.Cancel(request.runId);
					(void)localCancelRequested;

					{
						std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
						std::erase_if(
							m_chatRuntimeQueue,
							[&](const std::shared_ptr<ChatRuntimeJob>& queuedJob) {
								return queuedJob->request.runId == request.runId;
							});
						m_chatRuntimeJobsByRunId.erase(request.runId);
						m_chatRuntimeRunsById.erase(request.runId);
					}

					ClearEmbeddedRunCancelled(request.runId);
					ClearDeepSeekRunCancelled(request.runId);
					return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = activeModel,
						.errorCode = kChatRuntimeErrorTimedOut,
						.errorMessage = "chat runtime timed out",
					};
				}
				return job->result;
			});

		m_gatewayHost.SetChatAbortCallback([this](
			const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request) {
				std::shared_ptr<ChatRuntimeJob> queuedJob;
				std::string runProvider = m_activeChatProvider;
				bool removedQueuedJob = false;
				{
					std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
					auto stateIt = m_chatRuntimeRunsById.find(request.runId);
					if (stateIt != m_chatRuntimeRunsById.end()) {
						runProvider = stateIt->second.provider;
						stateIt->second.status = ChatRuntimeJobLifecycleStatus::Cancelled;
						stateIt->second.completedAtMs = CurrentEpochMs();
						stateIt->second.errorCode = kChatRuntimeErrorCancelled;
					}

					auto jobIt = m_chatRuntimeJobsByRunId.find(request.runId);
					if (jobIt != m_chatRuntimeJobsByRunId.end()) {
						queuedJob = jobIt->second;
						auto queueIt = std::find(
							m_chatRuntimeQueue.begin(),
							m_chatRuntimeQueue.end(),
							queuedJob);
						if (queueIt != m_chatRuntimeQueue.end()) {
							m_chatRuntimeQueue.erase(queueIt);
							removedQueuedJob = true;
							m_chatRuntimeJobsByRunId.erase(jobIt);
							m_chatRuntimeRunsById.erase(request.runId);
						}
					}
				}

				if (removedQueuedJob && queuedJob) {
					std::lock_guard<std::mutex> completionLock(queuedJob->completionMutex);
					queuedJob->result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = queuedJob->model,
						.errorCode = kChatRuntimeErrorCancelled,
						.errorMessage = "chat runtime cancelled",
					};
					queuedJob->completed = true;
					queuedJob->completionCv.notify_all();
				}

				MarkEmbeddedRunCancelled(request.runId);
				if (runProvider == "deepseek") {
					MarkDeepSeekRunCancelled(request.runId);
				}

				if (removedQueuedJob) {
					ClearEmbeddedRunCancelled(request.runId);
					ClearDeepSeekRunCancelled(request.runId);
				}

				bool cancelled = removedQueuedJob;
				if (m_localModelActivationEnabled) {
					cancelled = m_localModelRuntime.Cancel(request.runId) || cancelled;
					m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
				}

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

		AppendStartupTrace("ServiceManager.Start.gateway.beforeStart");
		const bool gatewayStartupEnabled = false;
		if (!gatewayStartupEnabled) {
			AppendStartupTrace("ServiceManager.Start.gateway.skipped");
			const bool startupLocalDispatchEnabled = ReadBoolEnvOrDefault(
				L"BLAZECLAW_GATEWAY_LOCAL_DISPATCH_ON_SKIP_ENABLED",
				true);
			if (startupLocalDispatchEnabled) {
				bool gatewayDispatchReady = false;
				try {
					gatewayDispatchReady =
						m_gatewayHost.StartLocalRuntimeDispatchOnly();
				}
				catch (...) {
					gatewayDispatchReady = false;
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"gateway local dispatch initialization threw an exception; continuing without local dispatch.");
					AppendStartupTrace("ServiceManager.Start.gateway.localRuntimeDispatch.exception");
				}
				if (!gatewayDispatchReady) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"gateway local dispatch initialization failed; methods may be unavailable.");
				}
				AppendStartupTrace(
					gatewayDispatchReady
					? "ServiceManager.Start.gateway.localRuntimeDispatch.ready"
					: "ServiceManager.Start.gateway.localRuntimeDispatch.failed");
			}
			else {
				AppendStartupTrace("ServiceManager.Start.gateway.localRuntimeDispatch.skipped");
			}
			const bool startupLocalInitEnabled = ReadBoolEnvOrDefault(
				L"BLAZECLAW_GATEWAY_LOCAL_INIT_ON_SKIP_ENABLED",
				false);
			if (startupLocalInitEnabled) {
				const bool gatewayLocalReady = m_gatewayHost.StartLocalOnly(config.gateway);
				if (!gatewayLocalReady) {
					m_skillsCatalog.diagnostics.warnings.push_back(
						L"gateway local runtime initialization failed; running with limited gateway methods.");
				}
				AppendStartupTrace("ServiceManager.Start.gateway.localInit.attempted");
			}
			else {
				AppendStartupTrace("ServiceManager.Start.gateway.localInit.skipped");
			}
			m_running = true;
			m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
			return true;
		}

		bool gatewayStarted = false;
		try {
			gatewayStarted = m_gatewayHost.Start(config.gateway);
		}
		catch (...) {
			gatewayStarted = false;
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"gateway startup threw an exception; running in degraded local mode.");
			AppendStartupTrace("ServiceManager.Start.gateway.exception");
		}

		if (!gatewayStarted) {
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"gateway startup failed; running in degraded local mode.");
			AppendStartupTrace("ServiceManager.Start.gateway.failed");
			m_running = true;
			m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
			return true;
		}

		m_running = true;
		AppendStartupTrace("ServiceManager.Start.gateway.afterStart");
		return true;
	}

	void ServiceManager::Stop() {
		m_skillsEnvOverrideService.RevertAll();
		if (m_chatRuntimeAsyncQueueEnabled) {
			StopChatRuntimeWorker();
		}
		m_gatewayHost.Stop();
		m_running = false;
	}

	bool ServiceManager::StartChatRuntimeWorker() {
		std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
		if (m_chatRuntimeWorkerThread.joinable()) {
			m_chatRuntimeWorkerAvailable = true;
			return true;
		}

		m_chatRuntimeWorkerStopRequested = false;
		try {
			m_chatRuntimeWorkerThread = std::thread([this]() {
				ChatRuntimeWorkerLoop();
				});
			m_chatRuntimeWorkerAvailable = true;
			return true;
		}
		catch (...) {
			m_chatRuntimeWorkerAvailable = false;
			return false;
		}
	}

	void ServiceManager::StopChatRuntimeWorker() {
		std::vector<std::shared_ptr<ChatRuntimeJob>> abandonedJobs;
		{
			std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
			m_chatRuntimeWorkerStopRequested = true;
			m_chatRuntimeWorkerAvailable = false;
			abandonedJobs.assign(
				m_chatRuntimeQueue.begin(),
				m_chatRuntimeQueue.end());
			m_chatRuntimeQueue.clear();
			m_chatRuntimeJobsByRunId.clear();
			for (const auto& job : abandonedJobs) {
				auto stateIt = m_chatRuntimeRunsById.find(job->request.runId);
				if (stateIt != m_chatRuntimeRunsById.end()) {
					stateIt->second.status = ChatRuntimeJobLifecycleStatus::Failed;
					stateIt->second.completedAtMs = CurrentEpochMs();
					stateIt->second.errorCode = kChatRuntimeErrorWorkerUnavailable;
				}
			}
		}

		m_chatRuntimeQueueCv.notify_all();

		for (const auto& job : abandonedJobs) {
			std::lock_guard<std::mutex> completionLock(job->completionMutex);
			job->result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = job->model,
				.errorCode = kChatRuntimeErrorWorkerUnavailable,
				.errorMessage = "chat runtime worker unavailable",
			};
			job->completed = true;
			job->completionCv.notify_all();
		}

		if (m_chatRuntimeWorkerThread.joinable()) {
			m_chatRuntimeWorkerThread.join();
		}

		std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
		m_chatRuntimeQueue.clear();
		m_chatRuntimeJobsByRunId.clear();
		m_chatRuntimeRunsById.clear();
		m_chatRuntimeNextEnqueueSequence = 1;
		m_chatRuntimeWorkerStopRequested = false;
	}

	void ServiceManager::ChatRuntimeWorkerLoop() {
		for (;;) {
			std::shared_ptr<ChatRuntimeJob> job;
			bool cancelledBeforeExecution = false;
			bool timedOutBeforeExecution = false;
			{
				std::unique_lock<std::mutex> lock(m_chatRuntimeQueueMutex);
				m_chatRuntimeQueueCv.wait(lock, [this]() {
					return m_chatRuntimeWorkerStopRequested || !m_chatRuntimeQueue.empty();
					});

				if (m_chatRuntimeWorkerStopRequested && m_chatRuntimeQueue.empty()) {
					break;
				}

				job = m_chatRuntimeQueue.front();
				m_chatRuntimeQueue.pop_front();
				auto stateIt = m_chatRuntimeRunsById.find(job->request.runId);
				if (stateIt != m_chatRuntimeRunsById.end()) {
					const std::uint64_t nowMs = CurrentEpochMs();
					cancelledBeforeExecution =
						stateIt->second.status == ChatRuntimeJobLifecycleStatus::Cancelled;
					timedOutBeforeExecution =
						nowMs > stateIt->second.enqueuedAtMs &&
						(nowMs - stateIt->second.enqueuedAtMs) >
						m_chatRuntimeQueueWaitTimeoutMs;
					stateIt->second.status = ChatRuntimeJobLifecycleStatus::Started;
					stateIt->second.startedAtMs = nowMs;
				}
			}

			blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
			if (cancelledBeforeExecution) {
				result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = job->model,
					.errorCode = kChatRuntimeErrorCancelled,
					.errorMessage = "chat runtime cancelled",
				};
			}
			else if (timedOutBeforeExecution) {
				result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = job->model,
					.errorCode = kChatRuntimeErrorTimedOut,
					.errorMessage = "chat runtime timed out before execution",
				};
			}
			else if (job->execute) {
				const std::uint64_t executeStartedAtMs = CurrentEpochMs();
				result = job->execute();
				const std::uint64_t executeCompletedAtMs = CurrentEpochMs();
				if (executeCompletedAtMs > executeStartedAtMs &&
					(executeCompletedAtMs - executeStartedAtMs) >
					m_chatRuntimeExecutionTimeoutMs) {
					result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = job->model,
						.errorCode = kChatRuntimeErrorTimedOut,
						.errorMessage = "chat runtime execution timed out",
					};
				}

				if (IsEmbeddedRunCancelled(job->request.runId) ||
					IsDeepSeekRunCancelled(job->request.runId)) {
					result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.assistantText = {},
						.modelId = job->model,
						.errorCode = kChatRuntimeErrorCancelled,
						.errorMessage = "chat runtime cancelled",
					};
				}
			}
			else {
				result = blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = job->model,
					.errorCode = kChatRuntimeErrorWorkerUnavailable,
					.errorMessage = "chat runtime worker unavailable",
				};
			}

			{
				std::lock_guard<std::mutex> lock(m_chatRuntimeQueueMutex);
				auto stateIt = m_chatRuntimeRunsById.find(job->request.runId);
				if (stateIt != m_chatRuntimeRunsById.end()) {
					stateIt->second.completedAtMs = CurrentEpochMs();
					stateIt->second.errorCode = result.errorCode;
					if (result.ok) {
						stateIt->second.status = ChatRuntimeJobLifecycleStatus::Completed;
					}
					else if (result.errorCode == kChatRuntimeErrorCancelled) {
						stateIt->second.status = ChatRuntimeJobLifecycleStatus::Cancelled;
					}
					else if (result.errorCode == kChatRuntimeErrorTimedOut) {
						stateIt->second.status = ChatRuntimeJobLifecycleStatus::TimedOut;
					}
					else {
						stateIt->second.status = ChatRuntimeJobLifecycleStatus::Failed;
					}

					m_chatRuntimeRunsById.erase(stateIt);
				}

				m_chatRuntimeJobsByRunId.erase(job->request.runId);
			}

			ClearEmbeddedRunCancelled(job->request.runId);
			ClearDeepSeekRunCancelled(job->request.runId);

			std::lock_guard<std::mutex> completionLock(job->completionMutex);
			job->result = std::move(result);
			job->completed = true;
			job->completionCv.notify_all();
		}
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
		const auto emailHealth =
			blazeclaw::gateway::executors::EmailScheduleExecutor::
			GetRuntimeHealthIndex(false);
		const std::uint64_t emailFallbackAttemptCount =
			m_embeddedRunFallbackCount;
		const std::uint64_t emailFallbackSuccessCount =
			emailFallbackAttemptCount >= m_embeddedRunFailureCount
			? (emailFallbackAttemptCount - m_embeddedRunFailureCount)
			: 0;
		const std::uint64_t emailFallbackFailureCount =
			emailFallbackAttemptCount >= emailFallbackSuccessCount
			? (emailFallbackAttemptCount - emailFallbackSuccessCount)
			: 0;
		std::size_t emailProbeReady = 0;
		std::size_t emailProbeUnavailable = 0;
		for (const auto& probe : emailHealth.probes) {
			if (probe.state == "ready") {
				++emailProbeReady;
				continue;
			}

			if (probe.state == "unavailable") {
				++emailProbeUnavailable;
			}
		}
		const std::uint64_t embeddedTotalRuns =
			m_embeddedRunSuccessCount + m_embeddedRunFailureCount;
		const double embeddedSuccessRate = embeddedTotalRuns == 0
			? 0.0
			: static_cast<double>(m_embeddedRunSuccessCount) /
			static_cast<double>(embeddedTotalRuns);
		const bool selfEvolvingReminderInjected =
			m_skillsPrompt.prompt.find(L"## Self-Evolving Reminder") !=
			std::wstring::npos;
		const bool configFeatureImplemented =
			m_registry.IsImplemented(L"embeddings-config-foundation");

		std::string report =
			"{\"runtime\":{\"running\":" + std::string(m_running ? "true" : "false") +
			",\"gatewayWarning\":\"" + m_gatewayHost.LastWarning() + "\"},"
			"\"emailFallback\":{\"preflightEnabled\":" +
			std::string(m_activeConfig.email.preflight.enabled ? "true" : "false") +
			",\"policyProfilesEnabled\":" +
			std::string(m_activeConfig.email.policyProfiles.enabled ? "true" : "false") +
			",\"policyProfilesEnforce\":" +
			std::string(m_activeConfig.email.policyProfiles.enforce ? "true" : "false") +
			",\"policyProfilesRuntimeEnabled\":" +
			std::string(m_emailPolicyRuntimeEnabled ? "true" : "false") +
			",\"policyProfilesRuntimeEnforce\":" +
			std::string(m_emailPolicyRuntimeEnforce ? "true" : "false") +
			",\"policyRolloutMode\":" +
			ToNarrow(m_emailPolicyRolloutMode) +
			"\",\"policyEnforceChannel\":" +
			ToNarrow(m_emailPolicyEnforceChannel) +
			"\",\"policyCanaryEligible\":" +
			std::string(m_emailPolicyCanaryEligible ? "true" : "false") +
			",\"rollbackBridgeEnabled\":" +
			std::string(m_emailPolicyRollbackBridgeEnabled ? "true" : "false") +
			",\"resolvedPolicyId\":\"" +
			ToNarrow(m_emailFallbackResolvedPolicy.profileId) +
			"\",\"resolvedBackends\":[\"" +
			(m_emailFallbackResolvedPolicy.backends.empty()
				? std::string("himalaya\",\"imap-smtp-email")
				: [&]() {
					std::string list;
					for (std::size_t i = 0;
						i < m_emailFallbackResolvedPolicy.backends.size();
						++i) {
						if (i > 0) {
							list += "\",\"";
						}
						list += ToNarrow(m_emailFallbackResolvedPolicy.backends[i]);
					}
					return list;
				}()) +
			"\"],\"policyActions\":{\"unavailable\":\"" +
					ToNarrow(m_emailFallbackResolvedPolicy.onUnavailable) +
					"\",\"authError\":\"" +
					ToNarrow(m_emailFallbackResolvedPolicy.onAuthError) +
					"\",\"execError\":\"" +
					ToNarrow(m_emailFallbackResolvedPolicy.onExecError) +
					"\"},\"retryMaxAttempts\":" +
					std::to_string(m_emailFallbackResolvedPolicy.retryMaxAttempts) +
					",\"retryDelayMs\":" +
					std::to_string(m_emailFallbackResolvedPolicy.retryDelayMs) +
					",\"requiresApproval\":" +
					std::string(m_emailFallbackResolvedPolicy.requiresApproval ? "true" : "false") +
					",\"approvalTokenTtlMinutes\":" +
					std::to_string(m_emailFallbackResolvedPolicy.approvalTokenTtlMinutes) +
					",\"capabilityState\":\"" +
					emailHealth.emailSendState +
					"\",\"healthGeneratedAtEpochMs\":" +
					std::to_string(emailHealth.generatedAtEpochMs) +
					",\"healthTtlMs\":" +
					std::to_string(emailHealth.ttlMs) +
					",\"probeReadyCount\":" +
					std::to_string(emailProbeReady) +
					",\"probeUnavailableCount\":" +
					std::to_string(emailProbeUnavailable) +
					",\"fallbackAttempts\":" +
					std::to_string(emailFallbackAttemptCount) +
					",\"fallbackSuccess\":" +
					std::to_string(emailFallbackSuccessCount) +
					",\"fallbackFailure\":" +
					std::to_string(emailFallbackFailureCount) +
					"},"
					"\"agents\":{\"count\":" + std::to_string(m_agentsScope.entries.size()) +
					",\"defaultAgent\":\"" + ToNarrow(m_agentsScope.defaultAgentId) + "\"},"
					"\"subagents\":{\"active\":" + std::to_string(m_subagentRegistry.activeRuns) +
					",\"pendingAnnounce\":" + std::to_string(m_subagentRegistry.pendingAnnounce) + "},"
					"\"acp\":{\"lastAllowed\":" +
					std::string(m_lastAcpDecision.allowed ? "true" : "false") +
					",\"reason\":\"" + m_lastAcpDecision.reason + "\"},"
					"\"embedded\":{\"activeRuns\":" + std::to_string(ActiveEmbeddedRuns()) +
					",\"dynamicLoopEnabled\":" +
					std::string(m_lastEmbeddedDynamicLoopEnabled ? "true" : "false") +
					",\"canaryEligible\":" +
					std::string(m_lastEmbeddedCanaryEligible ? "true" : "false") +
					",\"promotionReady\":" +
					std::string(m_lastEmbeddedPromotionReady ? "true" : "false") +
					",\"promotionMinRuns\":" +
					std::to_string(m_embeddedDynamicLoopPromotionMinRuns) +
					",\"promotionMinSuccessRate\":" +
					std::to_string(m_embeddedDynamicLoopPromotionMinSuccessRate) +
					",\"fallbackUsed\":" +
					std::string(m_lastEmbeddedFallbackUsed ? "true" : "false") +
					",\"fallbackReason\":\"" + m_lastEmbeddedFallbackReason +
					"\",\"totalRuns\":" + std::to_string(embeddedTotalRuns) +
					",\"successRate\":" + std::to_string(embeddedSuccessRate) +
					",\"runSuccess\":" + std::to_string(m_embeddedRunSuccessCount) +
					",\"runFailure\":" + std::to_string(m_embeddedRunFailureCount) +
					",\"runTimeout\":" + std::to_string(m_embeddedRunTimeoutCount) +
					",\"runCancelled\":" + std::to_string(m_embeddedRunCancelledCount) +
					",\"runFallback\":" + std::to_string(m_embeddedRunFallbackCount) +
					",\"taskDeltaTransitions\":" +
					std::to_string(m_embeddedTaskDeltaTransitionCount) + "},"
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

	bool ServiceManager::IsEmbeddedRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_embeddedCancelMutex);
		const auto it = m_embeddedCancelledRuns.find(runId);
		return it != m_embeddedCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.erase(runId);
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

	bool ServiceManager::IsEmbeddedDynamicLoopCanaryEligible(
		const std::string& provider,
		const std::string& sessionId) const {
		if (!m_activeConfig.embedded.dynamicToolLoopEnabled) {
			return false;
		}

		if (m_embeddedDynamicLoopCanaryProviders.empty() &&
			m_embeddedDynamicLoopCanarySessions.empty()) {
			return true;
		}

		const bool providerAllowed = m_embeddedDynamicLoopCanaryProviders.empty() ||
			ContainsCaseInsensitive(m_embeddedDynamicLoopCanaryProviders, provider);
		const bool sessionAllowed = m_embeddedDynamicLoopCanarySessions.empty() ||
			ContainsCaseInsensitive(m_embeddedDynamicLoopCanarySessions, sessionId);
		return providerAllowed && sessionAllowed;
	}

	bool ServiceManager::IsEmbeddedDynamicLoopPromotionReady() const {
		if (!m_activeConfig.embedded.dynamicToolLoopEnabled) {
			return false;
		}

		const std::uint64_t totalRuns =
			m_embeddedRunSuccessCount + m_embeddedRunFailureCount;
		if (totalRuns < m_embeddedDynamicLoopPromotionMinRuns) {
			return false;
		}

		const double successRate =
			static_cast<double>(m_embeddedRunSuccessCount) /
			static_cast<double>(totalRuns);
		return successRate >= m_embeddedDynamicLoopPromotionMinSuccessRate;
	}

	bool ServiceManager::ShouldFallbackFromEmbeddedFailure(
		const std::string& errorCode,
		const std::string& reason) const {
		const std::string normalizedError = ToLowerAscii(errorCode);
		const std::string normalizedReason = ToLowerAscii(reason);

		if (normalizedError == "embedded_deadline_exceeded" ||
			normalizedError == "embedded_loop_detected" ||
			normalizedError == "embedded_completion_failed" ||
			normalizedError == "embedded_tool_execution_failed") {
			return true;
		}

		return normalizedReason == "deadline_exceeded" ||
			normalizedReason == "tool_execution_failed" ||
			normalizedReason == "embedded_completion_failed";
	}

} // namespace blazeclaw::core
