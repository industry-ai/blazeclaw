#include "pch.h"
#include "ServiceManager.h"
#include "../app/CredentialStore.h"
#include "../app/BlazeClawMFCDoc.h"
#include "../app/BlazeClawMFCView.h"
#include "../app/MainFrame.h"

#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/executors/EmailScheduleExecutor.h"
#include "diagnostics/DiagnosticsSnapshot.h"
#include "diagnostics/DiagnosticsRegressionComparator.h"
#include "bootstrap/StartupFixtureValidator.h"
#include "tools/ToolArgumentValidators.h"
#include "tools/ToolProcessRunner.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <Windows.h>
#include <nlohmann/json.hpp>

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

		void DrainPipeAvailable(HANDLE readPipe, std::string& output) {
			if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE) {
				return;
			}

			for (;;) {
				DWORD available = 0;
				if (!PeekNamedPipe(
					readPipe,
					nullptr,
					0,
					nullptr,
					&available,
					nullptr) || available == 0) {
					break;
				}

				char buffer[4096]{};
				const DWORD toRead =
					available > sizeof(buffer)
					? static_cast<DWORD>(sizeof(buffer))
					: available;
				DWORD bytesRead = 0;
				if (!ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) ||
					bytesRead == 0) {
					break;
				}

				output.append(buffer, buffer + bytesRead);
			}
		}

		void AppendStartupTrace(const char* stage) {
			CServiceBootstrapCoordinator coordinator;
			coordinator.AppendStartupTrace(stage);
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

		std::string MaskSecretForTrace(const std::wstring& value) {
			if (value.empty()) {
				return "<empty>";
			}

			if (value.size() <= 4) {
				return "<len=" + std::to_string(value.size()) + ">";
			}

			const std::wstring masked =
				value.substr(0, 2) +
				L"***" +
				value.substr(value.size() - 2) +
				L"<len=" + std::to_wstring(value.size()) + L">";
			return ToNarrow(masked);
		}

		void EmitBaiduRuntimeDiagnostic(
			const char* stage,
			const std::string& detail) {
			const std::string safeStage =
				(stage == nullptr || std::string(stage).empty())
				? "unknown"
				: std::string(stage);
			TRACE(
				"[BaiduRuntime][%s] %s\n",
				safeStage.c_str(),
				detail.c_str());
		}

		std::string TruncateDiagnosticText(
			const std::string& value,
			const std::size_t maxChars = 1200) {
			if (value.size() <= maxChars) {
				return value;
			}

			if (maxChars <= 24) {
				return value.substr(0, maxChars);
			}

			return value.substr(0, maxChars - 24) + "...(truncated)";
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

		std::optional<std::wstring> ResolveBaiduApiKeyFromPersistedConfig() {
			auto trimLocal = [](const std::wstring& value) {
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
					return std::wstring{};
				}

				return std::wstring(first, last);
				};

			auto toLowerWideLocal = [](std::wstring value) {
				std::transform(
					value.begin(),
					value.end(),
					value.begin(),
					[](const wchar_t ch) {
						return static_cast<wchar_t>(std::towlower(ch));
					});
				return value;
				};

			std::vector<std::filesystem::path> candidates;

			const std::vector<std::wstring> configFolders = {
				L"baidu-search",
				L"baidu-search-search-web",
				L"baidu-search-search",
				L"baidu_search_search_web",
			};

			wchar_t profilePath[MAX_PATH]{};
			const DWORD chars = GetEnvironmentVariableW(
				L"USERPROFILE",
				profilePath,
				MAX_PATH);
			if (chars > 0 && chars < MAX_PATH) {
				for (const auto& folder : configFolders) {
					candidates.push_back(
						std::filesystem::path(profilePath) /
						L".config" /
						folder /
						L".env");
				}
			}

			std::error_code ec;
			const auto cwd = std::filesystem::current_path(ec);
			if (!ec) {
				candidates.push_back(
					cwd /
					L"blazeclaw" /
					L"skills" /
					L"baidu-search" /
					L".env");
				candidates.push_back(
					cwd /
					L"skills" /
					L"baidu-search" /
					L".env");
			}

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
				std::filesystem::path cursor =
					std::filesystem::path(modulePath).parent_path();
				while (!cursor.empty()) {
					candidates.push_back(
						cursor /
						L"blazeclaw" /
						L"skills" /
						L"baidu-search" /
						L".env");
					candidates.push_back(
						cursor /
						L"skills" /
						L"baidu-search" /
						L".env");

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

			for (const auto& path : candidates) {
				std::error_code existsError;
				if (!std::filesystem::exists(path, existsError) || existsError) {
					continue;
				}

				std::wifstream input(path);
				if (!input.is_open()) {
					continue;
				}

				std::wstring line;
				while (std::getline(input, line)) {
					const std::wstring trimmedLine = trimLocal(line);
					if (trimmedLine.empty() || trimmedLine.starts_with(L"#")) {
						continue;
					}

					const auto equals = trimmedLine.find(L'=');
					if (equals == std::wstring::npos || equals == 0) {
						continue;
					}

					const std::wstring key =
						toLowerWideLocal(trimLocal(trimmedLine.substr(0, equals)));
					std::wstring value = trimLocal(trimmedLine.substr(equals + 1));
					if (value.size() >= 2 &&
						((value.front() == L'"' && value.back() == L'"') ||
							(value.front() == L'\'' && value.back() == L'\''))) {
						value = value.substr(1, value.size() - 2);
					}

					if (value.empty()) {
						continue;
					}

					if (key == L"baidu_api_key" || key == L"api_key") {
						return value;
					}
				}
			}

			return std::nullopt;
		}

		void EnsureBaiduApiKeyRuntimeEnv() {
			wchar_t* inheritedValue = nullptr;
			std::size_t inheritedLength = 0;
			std::wstring inheritedKey;
			if (_wdupenv_s(
				&inheritedValue,
				&inheritedLength,
				L"BAIDU_API_KEY") == 0 &&
				inheritedValue != nullptr) {
				inheritedKey.assign(inheritedValue);
				free(inheritedValue);
			}

			const auto persisted = ResolveBaiduApiKeyFromPersistedConfig();
			if (persisted.has_value() && !persisted->empty()) {
				_wputenv_s(L"BAIDU_API_KEY", persisted.value().c_str());
				EmitBaiduRuntimeDiagnostic(
					"env",
					"BAIDU_API_KEY source=persisted set=true value=" +
					MaskSecretForTrace(persisted.value()));
				return;
			}

			if (!inheritedKey.empty()) {
				EmitBaiduRuntimeDiagnostic(
					"env",
					"BAIDU_API_KEY source=process set=false inherited=true value=" +
					MaskSecretForTrace(inheritedKey));
				return;
			}

			EmitBaiduRuntimeDiagnostic(
				"env",
				"BAIDU_API_KEY source=none set=false inherited=false value=<empty>");

			wchar_t* envValue = nullptr;
			std::size_t envLength = 0;
			if (_wdupenv_s(
				&envValue,
				&envLength,
				L"BAIDU_API_KEY") == 0 &&
				envValue != nullptr) {
				free(envValue);
			}
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


		bool ResolveHooksRemediationTelemetryEnabled(
			const blazeclaw::config::AppConfig& config) {
			return ReadBoolEnvOrDefault(
				L"BLAZENCLAW_HOOKS_REMEDIATION_TELEMETRY_ENABLED",
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


		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
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

		std::optional<std::filesystem::path> ResolveOpenClawWebBrowsingSkillRoot() {
			std::vector<std::filesystem::path> candidates;
			candidates.push_back(std::filesystem::current_path());

			wchar_t modulePath[MAX_PATH]{};
			if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
				candidates.push_back(std::filesystem::path(modulePath).parent_path());
			}

			for (const auto& root : candidates) {
				std::filesystem::path cursor = root;
				while (!cursor.empty()) {
					const auto candidateA =
						cursor /
						L"blazeclaw" /
						L"skills-openclaw-original" /
						L"web-browsing";
					if (std::filesystem::exists(candidateA / L"scripts" / L"search_web.py")) {
						return candidateA;
					}

					const auto candidateB =
						cursor /
						L"skills-openclaw-original" /
						L"web-browsing";
					if (std::filesystem::exists(candidateB / L"scripts" / L"search_web.py")) {
						return candidateB;
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

		void RegisterBaiduSearchRuntimeTools(blazeclaw::gateway::GatewayHost& host) {
			const auto skillRoot = ResolveBaiduSearchSkillRoot();
			for (const auto& spec : tools::BuildBaiduSearchToolRuntimeSpecs()) {
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

						EnsureBaiduApiKeyRuntimeEnv();
						if (!HasEnvVarValue(L"BAIDU_API_KEY")) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "baidu_api_key_missing";
							result.errorMessage =
								"BAIDU_API_KEY missing in runtime environment and persisted skill config.";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

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
						const auto cliArgs = tools::BuildBaiduSearchCliArgs(
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

						const auto process = tools::ExecutePythonSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							EmitBaiduRuntimeDiagnostic(
								"process_start_failed",
								"errorCode=" + process.errorCode +
								" message=" + process.errorMessage);
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
							EmitBaiduRuntimeDiagnostic(
								"process_timeout",
								"errorCode=" + process.errorCode +
								" output=" +
								TruncateDiagnosticText(process.output));
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
							tools::ClassifyBaiduFailureCode(process.output);
						const std::string normalizedFailure =
							classifiedFailure == "process_exit_nonzero"
							? "script_runtime_error"
							: classifiedFailure;
						EmitBaiduRuntimeDiagnostic(
							"process_nonzero",
							"exitCode=" +
							std::to_string(static_cast<unsigned long long>(process.exitCode)) +
							" classifiedFailure=" + classifiedFailure +
							" normalizedFailure=" + normalizedFailure +
							" output=" + TruncateDiagnosticText(process.output));
						result.status = normalizedFailure;
						result.errorCode = normalizedFailure;
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

		void RegisterContentPolishingRuntimeTools(
			blazeclaw::gateway::GatewayHost& host) {
			for (const auto& spec : tools::BuildContentPolishingToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "transform",
						.enabled = true,
					},
					[spec](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

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
								{ { "text", params.get<std::string>() } });
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

						const auto text = tools::ExtractTextArgument(params);
						if (!text.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "text is required";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						if (spec.id == "summarize.extract") {
							result.executed = true;
							result.status = "ok";
							result.result = tools::BuildSummarizeExtractOutput(text.value());
						}
						else {
							result.executed = true;
							result.status = "ok";
							result.result = tools::BuildHumanizerRewriteOutput(text.value());
						}

						result.errorCode.clear();
						result.errorMessage.clear();
						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;
						return result;
					});
			}
		}

		void RegisterImapSmtpRuntimeTools(blazeclaw::gateway::GatewayHost& host) {
			const auto skillRoot = ResolveImapSmtpSkillRoot();
			for (const auto& spec : tools::BuildImapSmtpToolRuntimeSpecs()) {
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
						const auto cliArgs = tools::BuildImapSmtpCliArgs(
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

						const auto process = tools::ExecuteNodeSkillProcess(
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
			const auto openClawWebBrowsingSkillRoot = ResolveOpenClawWebBrowsingSkillRoot();
			const bool enableOpenClawWebBrowsingFallback =
				ReadBoolEnvOrDefault(
					L"BLAZECLAW_WEB_BROWSING_ENABLE_OPENCLAW_FALLBACK",
					false);
			const bool requireApiKey = ResolveBraveRequireApiKey();
			const bool hasApiKey = HasEnvVarValue(L"BRAVE_API_KEY");
			for (const auto& spec : tools::BuildBraveSearchToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "search",
						.enabled = true,
					},
					[spec,
					skillRoot,
					openClawWebBrowsingSkillRoot,
					enableOpenClawWebBrowsingFallback,
					requireApiKey,
					hasApiKey](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
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
							if (tools::IsBraveSearchWebToolId(spec.id)) {
								params = nlohmann::json::object(
									{ {"query", params.get<std::string>()} });
							}
							else if (tools::IsBraveFetchContentToolId(spec.id)) {
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
						const auto cliArgs = tools::BuildBraveSearchCliArgs(
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
							tools::IsBraveSearchWebToolId(spec.id) ? 45000 : 30000;
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

						const auto process = tools::ExecuteNodeSkillProcess(
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
							tools::ClassifyBraveFailureCode(process.output);
						const bool canAttemptOpenClawFallback =
							enableOpenClawWebBrowsingFallback &&
							spec.id == "web_browsing.search.web" &&
							classifiedFailure == "network_error" &&
							tools::IsBraveNetworkTimeoutFailure(process.output) &&
							openClawWebBrowsingSkillRoot.has_value() &&
							params.contains("query") &&
							params["query"].is_string();

						if (canAttemptOpenClawFallback) {
							const std::string query =
								tools::TrimAsciiForBraveSearch(params["query"].get<std::string>());
							if (!query.empty()) {
								std::uint64_t fallbackTimeoutMs = 15000;
								if (request.deadlineEpochMs.has_value()) {
									const std::uint64_t now = CurrentEpochMs();
									if (request.deadlineEpochMs.value() <= now) {
										fallbackTimeoutMs = 0;
									}
									else {
										fallbackTimeoutMs = request.deadlineEpochMs.value() - now;
									}
								}

								if (fallbackTimeoutMs > 0) {
									const auto fallbackScriptPath =
										openClawWebBrowsingSkillRoot.value() /
										L"scripts" /
										L"search_web.py";
									if (std::filesystem::exists(fallbackScriptPath)) {
										const auto fallbackProcess = tools::ExecutePythonSkillProcess(
											fallbackScriptPath,
											std::vector<std::string>{ query },
											fallbackTimeoutMs);
										if (fallbackProcess.started &&
											!fallbackProcess.timedOut &&
											fallbackProcess.exitCode == 0) {
											result.executed = true;
											result.status = "ok";
											result.result =
												fallbackProcess.output +
												"\n[fallback=openclaw_python]";
											result.errorCode.clear();
											result.errorMessage.clear();
											result.completedAtMs = CurrentEpochMs();
											result.latencyMs =
												result.completedAtMs - result.startedAtMs;
											return result;
										}
									}
								}
							}
						}

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
		return m_skillsHooksCoordinator.BuildGatewaySkillsState(
			CSkillsHooksCoordinator::GatewayStateContext{
				.catalog = m_skillsCatalog,
				.eligibility = m_skillsEligibility,
				.prompt = m_skillsPrompt,
				.commands = m_skillsCommands,
				.watch = m_skillsWatch,
				.sync = m_skillsSync,
				.envOverrides = m_skillsEnvOverrides,
				.install = m_skillsInstall,
				.securityScan = m_skillSecurityScan,
				.hookExecution = m_hookExecution,
				.hooksGovernanceReportingEnabled = m_hooksGovernanceReportingEnabled,
				.hooksLastGovernanceReportPath = m_hooksLastGovernanceReportPath,
				.hooksGovernanceReportsGenerated = m_hooksGovernanceReportsGenerated,
				.hooksAutoRemediationEnabled = m_hooksAutoRemediationEnabled,
				.hooksAutoRemediationRequiresApproval = m_hooksAutoRemediationRequiresApproval,
				.hooksAutoRemediationExecuted = m_hooksAutoRemediationExecuted,
				.hooksLastAutoRemediationStatus = m_hooksLastAutoRemediationStatus,
				.hooksAutoRemediationTenantId = m_hooksAutoRemediationTenantId,
				.hooksLastAutoRemediationPlaybookPath = m_hooksLastAutoRemediationPlaybookPath,
				.hooksAutoRemediationTokenMaxAgeMinutes = m_hooksAutoRemediationTokenMaxAgeMinutes,
				.hooksAutoRemediationTokenRotations = m_hooksAutoRemediationTokenRotations,
				.hooksLastRemediationTelemetryPath = m_hooksLastRemediationTelemetryPath,
				.hooksLastRemediationAuditPath = m_hooksLastRemediationAuditPath,
				.hooksRemediationSloStatus = m_hooksRemediationSloStatus,
				.hooksRemediationSloMaxDriftDetected = m_hooksRemediationSloMaxDriftDetected,
				.hooksRemediationSloMaxPolicyBlocked = m_hooksRemediationSloMaxPolicyBlocked,
				.hooksLastComplianceAttestationPath = m_hooksLastComplianceAttestationPath,
				.hooksEnterpriseSlaPolicyId = m_hooksEnterpriseSlaPolicyId,
				.hooksCrossTenantAttestationAggregationEnabled =
					m_hooksCrossTenantAttestationAggregationEnabled,
				.hooksCrossTenantAttestationAggregationStatus =
					m_hooksCrossTenantAttestationAggregationStatus,
				.hooksCrossTenantAttestationAggregationCount =
					m_hooksCrossTenantAttestationAggregationCount,
				.hooksLastCrossTenantAttestationAggregationPath =
					m_hooksLastCrossTenantAttestationAggregationPath,
			},
			[this](
				const SkillsCatalogEntry& entry,
				const SkillsEligibilityEntry* eligibility,
				const SkillsCommandSpec* command,
				const SkillsInstallPlanEntry* install)
			{
				return BuildGatewaySkillEntry(entry, eligibility, command, install);
			},
			[](const std::wstring& value)
			{
				std::string output;
				output.reserve(value.size());
				for (const auto ch : value)
				{
					output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
				}
				return output;
			});
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
		m_skillsHooksCoordinator.RefreshSkillsState(
			config,
			forceRefresh,
			reason,
			CSkillsHooksCoordinator::RefreshContext{
				.catalogService = m_skillsCatalogService,
				.eligibilityService = m_skillsEligibilityService,
				.hookCatalogService = m_hookCatalogService,
				.hookExecutionService = m_hookExecutionService,
				.promptService = m_skillsPromptService,
				.hookEventService = m_hookEventService,
				.commandService = m_skillsCommandService,
				.syncService = m_skillsSyncService,
				.envOverrideService = m_skillsEnvOverrideService,
				.installService = m_skillsInstallService,
				.securityScanService = m_skillSecurityScanService,
				.watchService = m_skillsWatchService,
				.catalog = m_skillsCatalog,
				.eligibility = m_skillsEligibility,
				.hookCatalog = m_hookCatalog,
				.hookExecution = m_hookExecution,
				.prompt = m_skillsPrompt,
				.events = m_hookEvents,
				.commands = m_skillsCommands,
				.sync = m_skillsSync,
				.envOverrides = m_skillsEnvOverrides,
				.install = m_skillsInstall,
				.securityScan = m_skillSecurityScan,
				.watch = m_skillsWatch,
			 .workspaceRoot = ResolveWorkspaceRootForSkills(
					std::filesystem::current_path()),
				.hooksFallbackPromptInjection = m_hooksFallbackPromptInjection,
			});
	}

	bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
		if (m_running) {
			return true;
		}
		AppendStartupTrace("ServiceManager.Start.begin");

		ConfigurePolicies(config);
		InitializeModules();
		WireGatewayCallbacks();
		return FinalizeStartup(config);
	}

	void ServiceManager::ConfigurePolicies(
		const blazeclaw::config::AppConfig& config)
	{
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
	}

	void ServiceManager::InitializeModules()
	{
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
		m_sandbox = m_agentsSandboxService.BuildSnapshot(
			m_agentsScope,
			m_activeConfig);
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
		const auto runtimeQueueSettings =
			m_serviceBootstrapCoordinator.ResolveRuntimeQueueSettings(
				[](const wchar_t* key, const bool fallback)
				{
					return ReadBoolEnvOrDefault(key, fallback);
				},
				[](const wchar_t* key, const std::uint64_t fallback)
				{
					return ParseUInt64EnvValue(key, fallback);
				},
				kChatRuntimeQueueWaitTimeoutMs,
				kChatRuntimeExecutionTimeoutMs);
		m_chatRuntimeAsyncQueueEnabled = runtimeQueueSettings.asyncQueueEnabled;
		m_chatRuntimeQueueWaitTimeoutMs = runtimeQueueSettings.queueWaitTimeoutMs;
		m_chatRuntimeExecutionTimeoutMs = runtimeQueueSettings.executionTimeoutMs;
		m_chatRuntime.Initialize(
			CChatRuntime::Dependencies{
				.isRunCancelled = [this](
					const std::string& runId,
					const std::string& provider)
					{
						return IsEmbeddedRunCancelled(runId) ||
							(provider == "deepseek" && IsDeepSeekRunCancelled(runId));
					},
				.onQueueTimeout = [this](
					const std::string& runId,
					const std::string& provider)
					{
						MarkEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							MarkDeepSeekRunCancelled(runId);
						}
					},
				.onQueueTimeoutCleanup = [this](
					const std::string& runId,
					const std::string& provider)
					{
						ClearEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							ClearDeepSeekRunCancelled(runId);
						}
					},
				.onAbort = [this](
					const std::string& runId,
					const std::string& provider,
					const bool removedQueuedJob)
					{
						MarkEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							MarkDeepSeekRunCancelled(runId);
						}
						if (removedQueuedJob)
						{
							ClearEmbeddedRunCancelled(runId);
							ClearDeepSeekRunCancelled(runId);
						}
					},
				.onWorkerCompleted = [this](
					const std::string& runId,
					const std::string& provider)
					{
						ClearEmbeddedRunCancelled(runId);
						if (provider == "deepseek")
						{
							ClearDeepSeekRunCancelled(runId);
						}
					},
				.cancelActiveRuntime = [this](const std::string& runId)
					{
						if (m_localModelActivationEnabled)
						{
							const bool cancelled = m_localModelRuntime.Cancel(runId);
							m_localModelRuntimeSnapshot = m_localModelRuntime.Snapshot();
							return cancelled;
						}
						return false;
					},
			},
			CChatRuntime::Config{
				.queueCapacity = kChatRuntimeQueueCapacity,
				.queueWaitTimeoutMs = m_chatRuntimeQueueWaitTimeoutMs,
				.executionTimeoutMs = m_chatRuntimeExecutionTimeoutMs,
				.asyncQueueEnabled = m_chatRuntimeAsyncQueueEnabled,
				.errorQueueFull = runtime::contracts::kErrorQueueFull,
				.errorCancelled = runtime::contracts::kErrorCancelled,
				.errorTimedOut = runtime::contracts::kErrorTimedOut,
				.errorWorkerUnavailable = runtime::contracts::kErrorWorkerUnavailable,
			});
		if (m_chatRuntimeAsyncQueueEnabled) {
			const bool chatRuntimeWorkerStarted = m_chatRuntime.StartWorker();
			if (!chatRuntimeWorkerStarted) {
				TRACE(
					"[ChatRuntime] worker unavailable; callbacks will return %s\n",
					runtime::contracts::kErrorWorkerUnavailable);
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
				m_skillsHooksCoordinator.EmitGovernanceAndRemediation(
					HooksGovernanceEmitter::GovernanceContext{
						.execution = m_hookExecution,
						.governanceReportingEnabled = m_hooksGovernanceReportingEnabled,
						.governanceReportDir = m_hooksGovernanceReportDir,
						.allowedPackages = m_hooksAllowedPackages,
						.strictPolicyEnforcement = m_hooksStrictPolicyEnforcement,
						.governanceReportsGenerated = m_hooksGovernanceReportsGenerated,
						.lastGovernanceReportPath = m_hooksLastGovernanceReportPath,
					},
					HooksGovernanceEmitter::RemediationContext{
						.execution = m_hookExecution,
						.autoRemediationEnabled = m_hooksAutoRemediationEnabled,
						.autoRemediationTenantId = m_hooksAutoRemediationTenantId,
						.autoRemediationPlaybookDir =
							m_hooksAutoRemediationPlaybookDir,
						.autoRemediationTokenMaxAgeMinutes =
							m_hooksAutoRemediationTokenMaxAgeMinutes,
						.autoRemediationApprovalToken =
							m_hooksAutoRemediationApprovalToken,
						.autoRemediationTokenRotations =
							m_hooksAutoRemediationTokenRotations,
						.remediationTelemetryEnabled =
							m_hooksRemediationTelemetryEnabled,
						.remediationTelemetryDir = m_hooksRemediationTelemetryDir,
						.remediationAuditEnabled = m_hooksRemediationAuditEnabled,
						.remediationAuditDir = m_hooksRemediationAuditDir,
						.remediationSloMaxDriftDetected =
							m_hooksRemediationSloMaxDriftDetected,
						.remediationSloMaxPolicyBlocked =
							m_hooksRemediationSloMaxPolicyBlocked,
						.complianceAttestationEnabled =
							m_hooksComplianceAttestationEnabled,
						.complianceAttestationDir =
							m_hooksComplianceAttestationDir,
						.enterpriseSlaPolicyId = m_hooksEnterpriseSlaPolicyId,
						.crossTenantAttestationAggregationEnabled =
							m_hooksCrossTenantAttestationAggregationEnabled,
						.crossTenantAttestationAggregationDir =
							m_hooksCrossTenantAttestationAggregationDir,
						.lastGovernanceReportPath = m_hooksLastGovernanceReportPath,
						.lastAutoRemediationPlaybookPath =
							m_hooksLastAutoRemediationPlaybookPath,
						.lastAutoRemediationStatus =
							m_hooksLastAutoRemediationStatus,
						.lastRemediationTelemetryPath =
							m_hooksLastRemediationTelemetryPath,
						.lastRemediationAuditPath = m_hooksLastRemediationAuditPath,
						.remediationSloStatus = m_hooksRemediationSloStatus,
						.lastComplianceAttestationPath =
							m_hooksLastComplianceAttestationPath,
						.crossTenantAttestationAggregationCount =
							m_hooksCrossTenantAttestationAggregationCount,
						.crossTenantAttestationAggregationStatus =
							m_hooksCrossTenantAttestationAggregationStatus,
						.lastCrossTenantAttestationAggregationPath =
							m_hooksLastCrossTenantAttestationAggregationPath,
					},
					m_skillsCatalog.diagnostics.warnings);
				auto hookBootstrapProjection =
					CSkillsHooksCoordinator::HookBootstrapProjectionContext{
						.bootstrapFiles = m_hookExecution.bootstrapFiles,
						.prompt = m_skillsPrompt.prompt,
						.promptChars = m_skillsPrompt.promptChars,
						.promptTruncated = m_skillsPrompt.truncated,
						.maxSkillsPromptChars =
							m_activeConfig.skills.limits.maxSkillsPromptChars,
						.lastReminderState =
							m_hookExecution.diagnostics.lastReminderState,
						.lastReminderReason =
							m_hookExecution.diagnostics.lastReminderReason,
						.selfEvolvingHookTriggered = m_selfEvolvingHookTriggered,
				};
				m_skillsHooksCoordinator.ApplyHookBootstrapProjection(
					hookBootstrapProjection);
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

		const std::vector<std::filesystem::path> fixtureCandidates = {
			  std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
			  std::filesystem::current_path() / L"fixtures" / L"agents",
			  std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
			  std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
		};

		const bool startupFixtureValidationEnabled = ReadBoolEnvOrDefault(
			bootstrap::StartupFixtureValidator::kEnvStartupValidationEnabled,
			false);
		if (startupFixtureValidationEnabled) {
			m_serviceBootstrapCoordinator.ValidateStartupFixtures(
				CServiceBootstrapCoordinator::FixtureValidationContext{
					.enabled = startupFixtureValidationEnabled,
					.fixtureCandidates = fixtureCandidates,
					.warnings = m_skillsCatalog.diagnostics.warnings,
					.agentsCatalogService = m_agentsCatalogService,
					.agentsWorkspaceService = m_agentsWorkspaceService,
					.agentsToolPolicyService = m_agentsToolPolicyService,
					.agentsShellRuntimeService = m_agentsShellRuntimeService,
					.agentsModelRoutingService = m_agentsModelRoutingService,
					.agentsAuthProfileService = m_agentsAuthProfileService,
					.agentsSandboxService = m_agentsSandboxService,
					.agentsTranscriptSafetyService = m_agentsTranscriptSafetyService,
					.subagentRegistryService = m_subagentRegistryService,
					.acpSpawnService = m_acpSpawnService,
					.embeddingsService = m_embeddingsService,
					.retrievalMemoryService = m_retrievalMemoryService,
					.piEmbeddedService = m_piEmbeddedService,
					.skillsCatalogService = m_skillsCatalogService,
					.skillsEligibilityService = m_skillsEligibilityService,
					.skillsPromptService = m_skillsPromptService,
					.skillsCommandService = m_skillsCommandService,
					.skillsWatchService = m_skillsWatchService,
					.skillsSyncService = m_skillsSyncService,
					.skillsEnvOverrideService = m_skillsEnvOverrideService,
					.skillsInstallService = m_skillsInstallService,
					.skillSecurityScanService = m_skillSecurityScanService,
					.hookCatalogService = m_hookCatalogService,
					.hookEventService = m_hookEventService,
					.hookExecutionService = m_hookExecutionService,
				});
		}
		else {
			AppendStartupTrace("ServiceManager.Start.fixtures.validation.skipped");
		}
	}

	void ServiceManager::WireGatewayCallbacks()
	{
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
		m_toolRuntimeRegistry.RegisterAll(
			m_gatewayHost,
			CToolRuntimeRegistry::Dependencies{
				.registerImapSmtp = [](blazeclaw::gateway::GatewayHost& host) {
					RegisterImapSmtpRuntimeTools(host);
				},
				.registerContentPolishing = [](blazeclaw::gateway::GatewayHost& host) {
					RegisterContentPolishingRuntimeTools(host);
				},
				.registerBraveSearch = [](blazeclaw::gateway::GatewayHost& host) {
					RegisterBraveSearchRuntimeTools(host);
				},
				.registerBaiduSearch = [](blazeclaw::gateway::GatewayHost& host) {
					RegisterBaiduSearchRuntimeTools(host);
				},
			});

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
							.errorCode = runtime::contracts::kErrorCancelled,
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
							.enforceOrderedAllowlist = request.enforceOrderedAllowlist,
							.orderedAllowedToolTargets = request.orderedAllowedToolTargets,
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
						std::string streamedLocalText;
						std::vector<std::string> streamedLocalSnapshots;
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
							[&](const std::string& delta) {
								if (delta.empty()) {
									return;
								}

								streamedLocalText += delta;
								streamedLocalSnapshots.push_back(streamedLocalText);
								if (request.onAssistantDelta) {
									request.onAssistantDelta(streamedLocalText);
								}
							});
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

						std::string assistantText =
							streamedLocalText.empty()
							? localResult.text
							: streamedLocalText;
						std::string modelId = localResult.modelId;
						std::uint32_t latencyMs = localResult.latencyMs;
						std::uint32_t generatedTokens = localResult.generatedTokens;
						if (streamedLocalSnapshots.empty() &&
							IsLikelyEchoResponse(request.message, assistantText)) {
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
							.assistantDeltas = streamedLocalSnapshots,
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

				return m_chatRuntime.Execute(
					CChatRuntime::RuntimeExecutionRequest{
						.request = request,
						.sessionId = sessionId,
						.runtimeMessage = runtimeMessage,
						.provider = activeProvider,
						.model = activeModel,
						.execute = std::move(executeRequest),
					});
			});

		m_gatewayHost.SetChatAbortCallback([this](
			const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request) {
				const bool cancelled = m_chatRuntime.Abort(request);
				if (m_localModelActivationEnabled)
				{
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
	}

	bool ServiceManager::FinalizeStartup(
		const blazeclaw::config::AppConfig& config)
	{
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
			m_chatRuntime.StopWorker();
		}
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

		DiagnosticsSnapshot snapshot;
		snapshot.runtimeRunning = m_running;
		snapshot.gatewayWarning = m_gatewayHost.LastWarning();

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

		snapshot.emailPreflightEnabled = m_activeConfig.email.preflight.enabled;
		snapshot.emailPolicyProfilesEnabled = m_activeConfig.email.policyProfiles.enabled;
		snapshot.emailPolicyProfilesEnforce = m_activeConfig.email.policyProfiles.enforce;
		snapshot.emailPolicyProfilesRuntimeEnabled = m_emailPolicyRuntimeEnabled;
		snapshot.emailPolicyProfilesRuntimeEnforce = m_emailPolicyRuntimeEnforce;
		snapshot.emailPolicyRolloutMode = ToNarrow(m_emailPolicyRolloutMode);
		snapshot.emailPolicyEnforceChannel = ToNarrow(m_emailPolicyEnforceChannel);
		snapshot.emailPolicyCanaryEligible = m_emailPolicyCanaryEligible;
		snapshot.emailRollbackBridgeEnabled = m_emailPolicyRollbackBridgeEnabled;
		snapshot.emailResolvedPolicyId = ToNarrow(m_emailFallbackResolvedPolicy.profileId);
		for (const auto& backend : m_emailFallbackResolvedPolicy.backends) {
			snapshot.emailResolvedBackends.push_back(ToNarrow(backend));
		}
		snapshot.emailPolicyActionUnavailable = ToNarrow(m_emailFallbackResolvedPolicy.onUnavailable);
		snapshot.emailPolicyActionAuthError = ToNarrow(m_emailFallbackResolvedPolicy.onAuthError);
		snapshot.emailPolicyActionExecError = ToNarrow(m_emailFallbackResolvedPolicy.onExecError);
		snapshot.emailRetryMaxAttempts = m_emailFallbackResolvedPolicy.retryMaxAttempts;
		snapshot.emailRetryDelayMs = m_emailFallbackResolvedPolicy.retryDelayMs;
		snapshot.emailRequiresApproval = m_emailFallbackResolvedPolicy.requiresApproval;
		snapshot.emailApprovalTokenTtlMinutes = m_emailFallbackResolvedPolicy.approvalTokenTtlMinutes;
		snapshot.emailCapabilityState = emailHealth.emailSendState;
		snapshot.emailHealthGeneratedAtEpochMs = emailHealth.generatedAtEpochMs;
		snapshot.emailHealthTtlMs = emailHealth.ttlMs;
		for (const auto& probe : emailHealth.probes) {
			if (probe.state == "ready") {
				++snapshot.emailProbeReadyCount;
				continue;
			}

			if (probe.state == "unavailable") {
				++snapshot.emailProbeUnavailableCount;
			}
		}
		snapshot.emailFallbackAttempts = m_embeddedRunFallbackCount;
		snapshot.emailFallbackSuccess =
			snapshot.emailFallbackAttempts >= m_embeddedRunFailureCount
			? (snapshot.emailFallbackAttempts - m_embeddedRunFailureCount)
			: 0;
		snapshot.emailFallbackFailure =
			snapshot.emailFallbackAttempts >= snapshot.emailFallbackSuccess
			? (snapshot.emailFallbackAttempts - snapshot.emailFallbackSuccess)
			: 0;

		snapshot.agentsCount = m_agentsScope.entries.size();
		snapshot.agentsDefaultAgent = ToNarrow(m_agentsScope.defaultAgentId);
		snapshot.subagentsActive = m_subagentRegistry.activeRuns;
		snapshot.subagentsPendingAnnounce = m_subagentRegistry.pendingAnnounce;
		snapshot.acpLastAllowed = m_lastAcpDecision.allowed;
		snapshot.acpReason = m_lastAcpDecision.reason;

		snapshot.embeddedActiveRuns = ActiveEmbeddedRuns();
		snapshot.embeddedDynamicLoopEnabled = m_lastEmbeddedDynamicLoopEnabled;
		snapshot.embeddedCanaryEligible = m_lastEmbeddedCanaryEligible;
		snapshot.embeddedPromotionReady = m_lastEmbeddedPromotionReady;
		snapshot.embeddedPromotionMinRuns = m_embeddedDynamicLoopPromotionMinRuns;
		snapshot.embeddedPromotionMinSuccessRate = m_embeddedDynamicLoopPromotionMinSuccessRate;
		snapshot.embeddedFallbackUsed = m_lastEmbeddedFallbackUsed;
		snapshot.embeddedFallbackReason = m_lastEmbeddedFallbackReason;
		snapshot.embeddedTotalRuns = m_embeddedRunSuccessCount + m_embeddedRunFailureCount;
		snapshot.embeddedSuccessRate = snapshot.embeddedTotalRuns == 0
			? 0.0
			: static_cast<double>(m_embeddedRunSuccessCount) /
			static_cast<double>(snapshot.embeddedTotalRuns);
		snapshot.embeddedRunSuccess = m_embeddedRunSuccessCount;
		snapshot.embeddedRunFailure = m_embeddedRunFailureCount;
		snapshot.embeddedRunTimeout = m_embeddedRunTimeoutCount;
		snapshot.embeddedRunCancelled = m_embeddedRunCancelledCount;
		snapshot.embeddedRunFallback = m_embeddedRunFallbackCount;
		snapshot.embeddedTaskDeltaTransitions = m_embeddedTaskDeltaTransitionCount;

		snapshot.toolsPolicyEntries = m_agentsToolPolicy.entries.size();
		snapshot.toolsShellProcesses = ShellProcessCount();

		snapshot.modelPrimary = routing.primaryModel;
		snapshot.modelFallback = routing.fallbackModel;
		snapshot.modelFailovers = routing.failoverHistory.size();
		snapshot.authProfiles = auth.entries.size();

		snapshot.sandboxEnabledCount = sandbox.enabledCount;
		snapshot.sandboxBrowserEnabledCount = sandbox.browserEnabledCount;

		snapshot.embeddingsEnabled = embeddings.enabled;
		snapshot.embeddingsReady = embeddings.ready;
		snapshot.embeddingsProvider = embeddings.provider;
		snapshot.embeddingsStatus = embeddings.status;
		snapshot.embeddingsDimension = embeddings.dimension;
		snapshot.embeddingsMaxSequenceLength = embeddings.maxSequenceLength;
		snapshot.embeddingsModelPathConfigured = !embeddings.modelPath.empty();
		snapshot.embeddingsTokenizerPathConfigured = !embeddings.tokenizerPath.empty();
		snapshot.embeddingsConfigFeatureImplemented =
			m_registry.IsImplemented(L"embeddings-config-foundation");

		snapshot.localModelEnabled = localModel.enabled;
		snapshot.localModelReady = localModel.ready;
		snapshot.localModelRolloutEligible = m_localModelRolloutEligible;
		snapshot.localModelActivationEnabled = m_localModelActivationEnabled;
		snapshot.localModelActivationReason = m_localModelActivationReason;
		snapshot.localModelProvider = localModel.provider;
		snapshot.localModelRolloutStage = localModel.rolloutStage;
		snapshot.localModelStorageRoot = localModel.storageRoot;
		snapshot.localModelVersion = localModel.version;
		snapshot.localModelStatus = localModel.status;
		snapshot.localModelVerboseMetrics = localModel.verboseMetrics;
		snapshot.localModelRuntimeDllPresent = localModel.runtimeDllPresent;
		snapshot.localModelMaxTokens = localModel.maxTokens;
		snapshot.localModelTemperature = localModel.temperature;
		snapshot.localModelModelLoadAttempts = localModel.modelLoadAttempts;
		snapshot.localModelModelLoadFailures = localModel.modelLoadFailures;
		snapshot.localModelRequestsStarted = localModel.requestsStarted;
		snapshot.localModelRequestsCompleted = localModel.requestsCompleted;
		snapshot.localModelRequestsFailed = localModel.requestsFailed;
		snapshot.localModelRequestsCancelled = localModel.requestsCancelled;
		snapshot.localModelCumulativeTokens = localModel.cumulativeTokens;
		snapshot.localModelCumulativeLatencyMs = localModel.cumulativeLatencyMs;
		snapshot.localModelLastLatencyMs = localModel.lastLatencyMs;
		snapshot.localModelLastGeneratedTokens = localModel.lastGeneratedTokens;
		snapshot.localModelLastTokensPerSecond = localModel.lastTokensPerSecond;
		snapshot.localModelModelPathConfigured = !localModel.modelPath.empty();
		snapshot.localModelModelHashConfigured = !localModel.modelExpectedSha256.empty();
		snapshot.localModelModelHashVerified = localModel.modelHashVerified;
		snapshot.localModelTokenizerPathConfigured = !localModel.tokenizerPath.empty();
		snapshot.localModelTokenizerHashConfigured = !localModel.tokenizerExpectedSha256.empty();
		snapshot.localModelTokenizerHashVerified = localModel.tokenizerHashVerified;

		snapshot.retrievalEnabled = retrieval.enabled;
		snapshot.retrievalRecordCount = retrieval.recordCount;
		snapshot.retrievalLastQueryCount = retrieval.lastQueryCount;
		snapshot.retrievalStatus = retrieval.status;

		snapshot.skillsCatalogEntries = m_skillsCatalog.entries.size();
		snapshot.skillsPromptIncluded = m_skillsPrompt.includedCount;
		snapshot.skillsSelfEvolvingReminderInjected =
			m_skillsPrompt.prompt.find(L"## Self-Evolving Reminder") != std::wstring::npos;

		snapshot.hooksLoaded = m_hookCatalog.diagnostics.hooksLoaded;
		snapshot.hooksEngineMode = WideToNarrowAscii(m_hookExecution.diagnostics.engineMode);
		snapshot.hooksEngineEnabled = m_hooksEngineEnabled;
		snapshot.hooksFallbackPromptInjection = m_hooksFallbackPromptInjection;
		snapshot.hooksReminderEnabled = m_hooksReminderEnabled;
		snapshot.hooksReminderVerbosity = WideToNarrowAscii(m_hooksReminderVerbosity);
		snapshot.hooksStrictPolicyEnforcement = m_hooksStrictPolicyEnforcement;
		snapshot.hooksAllowedPackagesCount = m_hooksAllowedPackages.size();
		snapshot.hooksGovernanceReportingEnabled = m_hooksGovernanceReportingEnabled;
		snapshot.hooksGovernanceReportsGenerated = m_hooksGovernanceReportsGenerated;
		snapshot.hooksLastGovernanceReportPath = WideToNarrowAscii(m_hooksLastGovernanceReportPath);
		snapshot.hooksAutoRemediationEnabled = m_hooksAutoRemediationEnabled;
		snapshot.hooksAutoRemediationRequiresApproval = m_hooksAutoRemediationRequiresApproval;
		snapshot.hooksAutoRemediationExecuted = m_hooksAutoRemediationExecuted;
		snapshot.hooksLastAutoRemediationStatus = WideToNarrowAscii(m_hooksLastAutoRemediationStatus);
		snapshot.hooksAutoRemediationTenantId = WideToNarrowAscii(m_hooksAutoRemediationTenantId);
		snapshot.hooksLastAutoRemediationPlaybookPath = WideToNarrowAscii(m_hooksLastAutoRemediationPlaybookPath);
		snapshot.hooksAutoRemediationTokenMaxAgeMinutes = m_hooksAutoRemediationTokenMaxAgeMinutes;
		snapshot.hooksAutoRemediationTokenRotations = m_hooksAutoRemediationTokenRotations;
		snapshot.hooksRemediationTelemetryEnabled = m_hooksRemediationTelemetryEnabled;
		snapshot.hooksRemediationAuditEnabled = m_hooksRemediationAuditEnabled;
		snapshot.hooksLastRemediationTelemetryPath = WideToNarrowAscii(m_hooksLastRemediationTelemetryPath);
		snapshot.hooksLastRemediationAuditPath = WideToNarrowAscii(m_hooksLastRemediationAuditPath);
		snapshot.hooksRemediationSloStatus = WideToNarrowAscii(m_hooksRemediationSloStatus);
		snapshot.hooksRemediationSloMaxDriftDetected = m_hooksRemediationSloMaxDriftDetected;
		snapshot.hooksRemediationSloMaxPolicyBlocked = m_hooksRemediationSloMaxPolicyBlocked;
		snapshot.hooksComplianceAttestationEnabled = m_hooksComplianceAttestationEnabled;
		snapshot.hooksLastComplianceAttestationPath = WideToNarrowAscii(m_hooksLastComplianceAttestationPath);
		snapshot.hooksEnterpriseSlaGovernanceEnabled = m_hooksEnterpriseSlaGovernanceEnabled;
		snapshot.hooksEnterpriseSlaPolicyId = WideToNarrowAscii(m_hooksEnterpriseSlaPolicyId);
		snapshot.hooksCrossTenantAttestationAggregationEnabled = m_hooksCrossTenantAttestationAggregationEnabled;
		snapshot.hooksCrossTenantAttestationAggregationStatus = WideToNarrowAscii(m_hooksCrossTenantAttestationAggregationStatus);
		snapshot.hooksCrossTenantAttestationAggregationCount = m_hooksCrossTenantAttestationAggregationCount;
		snapshot.hooksLastCrossTenantAttestationAggregationPath = WideToNarrowAscii(m_hooksLastCrossTenantAttestationAggregationPath);
		snapshot.hooksSelfEvolvingHookTriggered = m_selfEvolvingHookTriggered;
		snapshot.hooksInvalidMetadata = m_hookCatalog.diagnostics.invalidMetadataFiles;
		snapshot.hooksUnsafeHandlerPaths = m_hookCatalog.diagnostics.unsafeHandlerPaths;
		snapshot.hooksMissingHandlers = m_hookCatalog.diagnostics.missingHandlerFiles;
		snapshot.hooksEventsEmitted = m_hookEvents.diagnostics.emittedCount;
		snapshot.hooksEventValidationFailed = m_hookEvents.diagnostics.validationFailedCount;
		snapshot.hooksEventsDropped = m_hookEvents.diagnostics.droppedCount;
		snapshot.hooksDispatches = m_hookExecution.diagnostics.dispatchCount;
		snapshot.hooksHookDispatchCount = m_hookExecution.diagnostics.dispatchCount;
		snapshot.hooksDispatchSuccess = m_hookExecution.diagnostics.successCount;
		snapshot.hooksDispatchFailures = m_hookExecution.diagnostics.failureCount;
		snapshot.hooksHookFailureCount = m_hookExecution.diagnostics.failureCount;
		snapshot.hooksDispatchSkipped = m_hookExecution.diagnostics.skippedCount;
		snapshot.hooksDispatchTimeouts = m_hookExecution.diagnostics.timeoutCount;
		snapshot.hooksGuardRejected = m_hookExecution.diagnostics.guardRejectedCount;
		snapshot.hooksReminderTriggered = m_hookExecution.diagnostics.reminderTriggeredCount;
		snapshot.hooksReminderInjected = m_hookExecution.diagnostics.reminderInjectedCount;
		snapshot.hooksReminderSkipped = m_hookExecution.diagnostics.reminderSkippedCount;
		snapshot.hooksPolicyBlocked = m_hookExecution.diagnostics.policyBlockedCount;
		snapshot.hooksDriftDetected = m_hookExecution.diagnostics.driftDetectedCount;
		snapshot.hooksLastDriftReason = WideToNarrowAscii(m_hookExecution.diagnostics.lastDriftReason);
		snapshot.hooksReminderState = WideToNarrowAscii(m_hookExecution.diagnostics.lastReminderState);
		snapshot.hooksReminderReason = WideToNarrowAscii(m_hookExecution.diagnostics.lastReminderReason);

		snapshot.featuresImplemented = implementedCount;
		snapshot.featuresInProgress = inProgressCount;
		snapshot.featuresPlanned = plannedCount;
		snapshot.featuresRegistryState = featureStateLabel(
			m_registry.Features().empty()
			? FeatureState::Planned
			: m_registry.Features().front().state);

		return m_diagnosticsReportBuilder.BuildOperatorDiagnosticsReport(snapshot);
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


	blazeclaw::gateway::GatewayHost::ChatRuntimeResult
		ServiceManager::InvokeDeepSeekRemoteChat(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& modelId,
			const std::string& apiKey) const {
		return m_deepSeekClient.InvokeChat(
			CDeepSeekClient::ChatRequest{
				.runId = request.runId,
				.sessionKey = request.sessionKey,
				.message = request.message,
				.modelId = modelId,
				.apiKey = apiKey,
				.onAssistantDelta = request.onAssistantDelta,
			},
			[this](const std::string& runId)
			{
				return IsDeepSeekRunCancelled(runId);
			});
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
