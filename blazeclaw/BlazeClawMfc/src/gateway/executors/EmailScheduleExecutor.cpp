#include "pch.h"
#include "EmailScheduleExecutor.h"

#include "../ApprovalTokenStore.h"
#include "../GatewayJsonUtils.h"
#include "../GatewayPersistencePaths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <vector>
#include <windows.h>

namespace blazeclaw::gateway::executors {
	namespace {
		std::string ReadEnvVar(const char* name);
		std::string ToLowerCopy(const std::string& value);
		bool HasHimalayaBinary();
		bool HasNodeBinary();
		bool HasImapSmtpSkill();

		enum class EmailDeliveryFailureClass {
			Unavailable,
			AuthError,
			ExecError,
		};

		struct EmailDeliveryResolverPlan {
			std::vector<std::string> backends;
			std::string onUnavailable = "continue";
			std::string onAuthError = "stop";
			std::string onExecError = "retry_then_continue";
			std::uint32_t retryMaxAttempts = 1;
			std::uint32_t retryDelayMs = 0;
			bool enforcePolicy = false;
		};

		constexpr std::uint64_t kDefaultPreflightTtlMs = 60 * 1000;

		bool ParseBoolEnvOrDefault(
			const char* name,
			const bool fallback) {
			const std::string value = ToLowerCopy(json::Trim(ReadEnvVar(name)));
			if (value == "1" || value == "true" || value == "yes" || value == "on") {
				return true;
			}

			if (value == "0" || value == "false" || value == "no" || value == "off") {
				return false;
			}

			return fallback;
		}

		std::uint32_t ParseUIntEnvOrDefault(
			const char* name,
			const std::uint32_t fallback) {
			const std::string raw = json::Trim(ReadEnvVar(name));
			if (raw.empty()) {
				return fallback;
			}

			try {
				return static_cast<std::uint32_t>(std::stoul(raw));
			}
			catch (...) {
				return fallback;
			}
		}

		std::uint64_t ResolvePreflightTtlMs() {
			const std::string raw = json::Trim(ReadEnvVar("BLAZECLAW_EMAIL_PREFLIGHT_TTL_MS"));
			if (raw.empty()) {
				return kDefaultPreflightTtlMs;
			}

			try {
				const auto parsed = static_cast<std::uint64_t>(std::stoull(raw));
				if (parsed == 0) {
					return kDefaultPreflightTtlMs;
				}
				return parsed;
			}
			catch (...) {
				return kDefaultPreflightTtlMs;
			}
		}

		DependencyProbeResult BuildProbe(
			const std::string& key,
			const bool available,
			const std::string& unavailableCode,
			const std::string& unavailableMessage,
			const std::uint64_t checkedAt,
			const std::uint64_t ttlMs) {
			DependencyProbeResult result;
			result.key = key;
			result.state = available ? "ready" : "unavailable";
			result.reasonCode = available ? "ok" : unavailableCode;
			result.reasonMessage = available ? "ok" : unavailableMessage;
			result.checkedAtEpochMs = checkedAt;
			result.expiresAtEpochMs = checkedAt + ttlMs;
			return result;
		}

		RuntimeHealthIndex BuildRuntimeHealthIndexSnapshot(
			const std::uint64_t checkedAt,
			const std::uint64_t ttlMs) {
			RuntimeHealthIndex index;
			index.generatedAtEpochMs = checkedAt;
			index.ttlMs = ttlMs;

			const bool hasHimalaya = HasHimalayaBinary();
			const bool hasNode = HasNodeBinary();
			const bool hasImapSmtp = HasImapSmtpSkill();

			index.probes.push_back(BuildProbe(
				"backend:himalaya",
				hasHimalaya,
				"himalaya_cli_missing",
				"himalaya cli not found",
				checkedAt,
				ttlMs));

			index.probes.push_back(BuildProbe(
				"runtime:node",
				hasNode,
				"node_cli_missing",
				"node runtime not found",
				checkedAt,
				ttlMs));

			index.probes.push_back(BuildProbe(
				"skill:imap_smtp_email",
				hasImapSmtp,
				"imap_smtp_skill_missing",
				"imap smtp skill scripts not found",
				checkedAt,
				ttlMs));

			if (hasHimalaya || (hasNode && hasImapSmtp)) {
				index.emailSendState = "ready";
			}
			else {
				index.emailSendState = "unavailable";
			}

			return index;
		}

		RuntimeHealthIndex BuildFallbackHealthIndex(
			const std::uint64_t checkedAt,
			const std::uint64_t ttlMs) {
			RuntimeHealthIndex index;
			index.generatedAtEpochMs = checkedAt;
			index.ttlMs = ttlMs;
			index.emailSendState = "unknown";
			return index;
		}

		RuntimeHealthIndex& MutableHealthIndexCache() {
			static RuntimeHealthIndex cache{};
			return cache;
		}

		std::mutex& HealthIndexCacheMutex() {
			static std::mutex mutex;
			return mutex;
		}
		std::string EscapeJson(const std::string& value) {
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

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		ApprovalTokenStore& SharedApprovalStore() {
			static ApprovalTokenStore store;
			static std::once_flag initFlag;

			std::call_once(initFlag, []() {
				store.Initialize(
					ResolveGatewayStateFilePath("approvals.json").string());
				});

			return store;
		}

		std::string BuildNeedsApprovalEnvelope(
			const std::string& recipient,
			const std::string& subject,
			const std::string& sendAt,
			const std::string& approvalToken,
			const std::uint64_t expiresAtEpochMs) {
			return std::string("{\"protocolVersion\":1,\"ok\":true,\"status\":\"needs_approval\",\"output\":[],\"requiresApproval\":{\"type\":\"approval_request\",\"prompt\":\"Approve outbound email scheduling?\",\"items\":[{\"to\":\"") +
				EscapeJson(recipient) +
				"\",\"subject\":\"" +
				EscapeJson(subject) +
				"\",\"sendAt\":\"" +
				EscapeJson(sendAt) +
				"\"}],\"approvalToken\":\"" +
				EscapeJson(approvalToken) +
				"\",\"approvalTokenExpiresAtEpochMs\":" +
				std::to_string(expiresAtEpochMs) +
				"}}";
		}

		std::string BuildResultEnvelope(
			const bool approved,
			const bool delivered,
			const std::string& recipient,
			const std::string& subject,
			const std::string& sendAt,
			const std::string& backend,
			const std::string& transportStatus,
			const std::string& transportOutput) {
			if (!approved) {
				return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"cancelled\",\"output\":[],\"requiresApproval\":null}";
			}

			return std::string("{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\",\"output\":[{\"summary\":{\"to\":\"") +
				EscapeJson(recipient) +
				"\",\"subject\":\"" +
				EscapeJson(subject) +
				"\",\"sendAt\":\"" +
				EscapeJson(sendAt) +
				"\",\"scheduled\":true,\"delivered\":" +
				std::string(delivered ? "true" : "false") +
				",\"engine\":\"" +
				EscapeJson(backend) +
				"\",\"transportStatus\":\"" +
				EscapeJson(transportStatus) +
				"\",\"transportOutput\":\"" +
				EscapeJson(transportOutput) +
				"\"}}],\"requiresApproval\":null}";
		}

		std::string BuildErrorEnvelope(
			const std::string& code,
			const std::string& message) {
			return std::string("{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\",\"output\":[],\"requiresApproval\":null,\"error\":{\"code\":\"") +
				EscapeJson(code) +
				"\",\"message\":\"" +
				EscapeJson(message) +
				"\"}}";
		}

		std::string ReadEnvVar(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			std::size_t required = 0;
			if (_dupenv_s(&raw, &required, name) != 0 ||
				raw == nullptr ||
				required == 0) {
				if (raw != nullptr) {
					free(raw);
				}
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		std::string ToLowerCopy(const std::string& value) {
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

		std::vector<std::string> SplitValues(const std::string& raw) {
			std::vector<std::string> values;
			std::string token;
			for (const char ch : raw) {
				if (ch == ',' || ch == ';') {
					const std::string trimmed = json::Trim(token);
					if (!trimmed.empty()) {
						values.push_back(trimmed);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::string trimmed = json::Trim(token);
			if (!trimmed.empty()) {
				values.push_back(trimmed);
			}

			return values;
		}

		std::string QuoteArg(const std::string& value) {
			if (value.find_first_of(" \t\"") == std::string::npos) {
				return value;
			}

			std::string escaped = "\"";
			for (const char ch : value) {
				if (ch == '"') {
					escaped += "\\\"";
					continue;
				}
				escaped.push_back(ch);
			}
			escaped += "\"";
			return escaped;
		}

		std::optional<std::string> ResolveExecutablePath(
			const std::string& executable) {
			const std::string trimmed = json::Trim(executable);
			if (trimmed.empty()) {
				return std::nullopt;
			}

			const bool directPath =
				trimmed.find('\\') != std::string::npos ||
				trimmed.find('/') != std::string::npos ||
				trimmed.find(':') != std::string::npos;
			if (directPath) {
				std::error_code ec;
				if (std::filesystem::exists(trimmed, ec) && !ec) {
					const auto canonical = std::filesystem::weakly_canonical(
						std::filesystem::path(trimmed),
						ec);
					if (!ec) {
						return canonical.string();
					}
					return trimmed;
				}
				return std::nullopt;
			}

			char resolved[MAX_PATH] = {};
			const DWORD chars = SearchPathA(
				nullptr,
				trimmed.c_str(),
				nullptr,
				MAX_PATH,
				resolved,
				nullptr);
			if (chars == 0 || chars >= MAX_PATH) {
				return std::nullopt;
			}

			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(
				std::filesystem::path(resolved),
				ec);
			if (!ec) {
				return canonical.string();
			}

			return std::string(resolved);
		}

		std::string NormalizeMode() {
			const auto mode =
				ToLowerCopy(json::Trim(ReadEnvVar("BLAZECLAW_EMAIL_DELIVERY_MODE")));
			if (mode == "mock_success" || mode == "mock_failure") {
				return mode;
			}

			return "auto";
		}

		std::string NormalizeImapSmtpMode() {
			const auto mode = ToLowerCopy(
				json::Trim(ReadEnvVar("BLAZECLAW_EMAIL_IMAP_SMTP_MODE")));
			if (mode == "mock_success" || mode == "mock_failure") {
				return mode;
			}

			return "auto";
		}

		std::vector<std::string> ResolveEmailBackendChain() {
			const std::string configured = ToLowerCopy(
				json::Trim(ReadEnvVar("BLAZECLAW_EMAIL_DELIVERY_BACKENDS")));
			std::vector<std::string> chain;
			if (!configured.empty()) {
				for (const auto& value : SplitValues(configured)) {
					const std::string lowered = ToLowerCopy(json::Trim(value));
					if (!lowered.empty()) {
						chain.push_back(lowered);
					}
				}
			}

			if (chain.empty()) {
				chain.push_back("himalaya");
				chain.push_back("imap-smtp-email");
			}

			return chain;
		}

		std::string NormalizePolicyAction(
			const std::string& raw,
			const std::string& fallback) {
			const std::string normalized = ToLowerCopy(json::Trim(raw));
			if (normalized == "continue" ||
				normalized == "stop" ||
				normalized == "retry_then_continue") {
				return normalized;
			}

			return fallback;
		}

		EmailDeliveryFailureClass ClassifyDeliveryFailure(
			const std::string& errorCode,
			const std::string& errorMessage) {
			const std::string code = ToLowerCopy(errorCode);
			const std::string message = ToLowerCopy(errorMessage);

			const bool unavailable =
				code.find("missing") != std::string::npos ||
				code.find("unavailable") != std::string::npos ||
				message.find("not found") != std::string::npos;
			if (unavailable) {
				return EmailDeliveryFailureClass::Unavailable;
			}

			const bool authError =
				code.find("auth") != std::string::npos ||
				code.find("credential") != std::string::npos ||
				code.find("token") != std::string::npos ||
				message.find("auth") != std::string::npos ||
				message.find("credential") != std::string::npos;
			if (authError) {
				return EmailDeliveryFailureClass::AuthError;
			}

			return EmailDeliveryFailureClass::ExecError;
		}

		EmailDeliveryResolverPlan ResolveEmailDeliveryResolverPlan() {
			EmailDeliveryResolverPlan plan;

			const bool profilesEnabled = ParseBoolEnvOrDefault(
				"BLAZECLAW_EMAIL_POLICY_PROFILES_ENABLED",
				false);
			const bool profilesEnforce = ParseBoolEnvOrDefault(
				"BLAZECLAW_EMAIL_POLICY_PROFILES_ENFORCE",
				false);
			plan.enforcePolicy = profilesEnabled && profilesEnforce;

			const std::string configuredBackends = json::Trim(
				ReadEnvVar("BLAZECLAW_EMAIL_POLICY_BACKENDS"));
			if (!configuredBackends.empty()) {
				for (const auto& value : SplitValues(configuredBackends)) {
					const std::string normalized = ToLowerCopy(json::Trim(value));
					if (!normalized.empty()) {
						plan.backends.push_back(normalized);
					}
				}
			}
			if (plan.backends.empty()) {
				plan.backends = ResolveEmailBackendChain();
			}

			plan.onUnavailable = NormalizePolicyAction(
				ReadEnvVar("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE"),
				"continue");
			plan.onAuthError = NormalizePolicyAction(
				ReadEnvVar("BLAZECLAW_EMAIL_POLICY_ACTION_AUTH_ERROR"),
				"stop");
			plan.onExecError = NormalizePolicyAction(
				ReadEnvVar("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR"),
				"retry_then_continue");

			plan.retryMaxAttempts = (std::clamp)(
				ParseUIntEnvOrDefault(
					"BLAZECLAW_EMAIL_POLICY_RETRY_MAX_ATTEMPTS",
					1),
				std::uint32_t{ 1 },
				std::uint32_t{ 8 });
			plan.retryDelayMs = (std::min)(
				ParseUIntEnvOrDefault(
					"BLAZECLAW_EMAIL_POLICY_RETRY_DELAY_MS",
					0),
				std::uint32_t{ 300000 });

			return plan;
		}

		std::string ResolvePolicyActionForFailure(
			const EmailDeliveryResolverPlan& plan,
			const EmailDeliveryFailureClass failureClass) {
			if (!plan.enforcePolicy) {
				return "continue";
			}

			switch (failureClass) {
			case EmailDeliveryFailureClass::Unavailable:
				return NormalizePolicyAction(plan.onUnavailable, "continue");
			case EmailDeliveryFailureClass::AuthError:
				return NormalizePolicyAction(plan.onAuthError, "stop");
			case EmailDeliveryFailureClass::ExecError:
			default:
				return NormalizePolicyAction(plan.onExecError, "retry_then_continue");
			}
		}

		std::string ResolveImapSmtpSkillRoot() {
			const std::vector<std::filesystem::path> candidates = {
				std::filesystem::path("blazeclaw") / "skills" / "imap-smtp-email",
				std::filesystem::path("skills") / "imap-smtp-email",
				std::filesystem::path("openclaw") / "skills" / "imap-smtp-email",
			};

			for (const auto& candidate : candidates) {
				std::error_code ec;
				if (std::filesystem::exists(candidate, ec) &&
					std::filesystem::is_directory(candidate, ec) &&
					!ec) {
					return candidate.string();
				}
			}

			return {};
		}

		bool HasHimalayaBinary() {
			const DWORD withExe = SearchPathA(
				nullptr,
				"himalaya.exe",
				nullptr,
				0,
				nullptr,
				nullptr);
			if (withExe > 0) {
				return true;
			}

			const DWORD plain = SearchPathA(
				nullptr,
				"himalaya",
				nullptr,
				0,
				nullptr,
				nullptr);
			return plain > 0;
		}

		bool HasNodeBinary() {
			return ResolveExecutablePath("node").has_value();
		}

		bool HasImapSmtpSkill() {
			const std::string root = ResolveImapSmtpSkillRoot();
			if (root.empty()) {
				return false;
			}

			std::error_code ec;
			const auto scriptPath =
				std::filesystem::path(root) / "scripts" / "smtp.js";
			return std::filesystem::exists(scriptPath, ec) && !ec;
		}

		bool IsSafeAccountName(const std::string& account) {
			for (const char ch : account) {
				const bool isAlphaNum =
					(ch >= 'a' && ch <= 'z') ||
					(ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9');
				if (isAlphaNum || ch == '-' || ch == '_' || ch == '.') {
					continue;
				}

				return false;
			}

			return !account.empty();
		}

		std::string BuildEmailTemplate(
			const std::string& recipient,
			const std::string& subject,
			const std::string& body) {
			std::string templateText;
			templateText.reserve(
				recipient.size() +
				subject.size() +
				body.size() +
				64);
			templateText += "To: ";
			templateText += recipient;
			templateText += "\nSubject: ";
			templateText += subject;
			templateText += "\n\n";
			templateText += body;
			templateText += "\n";
			return templateText;
		}

		bool WriteFileUtf8(
			const std::filesystem::path& filePath,
			const std::string& text) {
			std::error_code ec;
			std::filesystem::create_directories(filePath.parent_path(), ec);

			std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
			if (!out.is_open()) {
				return false;
			}

			out.write(text.data(), static_cast<std::streamsize>(text.size()));
			return out.good();
		}

		bool RunCommandWithOutput(
			const std::string& command,
			std::string& outStdout,
			int& outExitCode) {
			outStdout.clear();
			outExitCode = -1;

			FILE* pipe = _popen(command.c_str(), "r");
			if (pipe == nullptr) {
				return false;
			}

			std::array<char, 512> buffer{};
			while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
				outStdout += buffer.data();
			}

			outExitCode = _pclose(pipe);
			return true;
		}

		bool DeliverViaHimalaya(
			const std::string& recipient,
			const std::string& subject,
			const std::string& body,
			const std::string& account,
			std::string& outStatus,
			std::string& outOutput,
			std::string& outErrorCode,
			std::string& outErrorMessage) {
			outStatus.clear();
			outOutput.clear();
			outErrorCode.clear();
			outErrorMessage.clear();

			const std::string mode = NormalizeMode();
			if (mode == "mock_success") {
				outStatus = "sent";
				outOutput = "mock_success";
				return true;
			}

			if (mode == "mock_failure") {
				outErrorCode = "himalaya_send_failed";
				outErrorMessage = "mock_failure";
				return false;
			}

			if (!HasHimalayaBinary()) {
				outErrorCode = "himalaya_cli_missing";
				outErrorMessage = "himalaya_cli_missing";
				return false;
			}

			const std::string normalizedAccount = json::Trim(account);
			if (!normalizedAccount.empty() && !IsSafeAccountName(normalizedAccount)) {
				outErrorCode = "invalid_account";
				outErrorMessage = "invalid_himalaya_account";
				return false;
			}

			const auto nowEpochMs = CurrentEpochMs();
			const std::filesystem::path inputPath =
				std::filesystem::temp_directory_path() /
				("blazeclaw_email_" + std::to_string(nowEpochMs) + ".mml");

			const std::string templateText = BuildEmailTemplate(
				recipient,
				subject,
				body);
			if (!WriteFileUtf8(inputPath, templateText)) {
				outErrorCode = "email_temp_write_failed";
				outErrorMessage = "email_temp_write_failed";
				return false;
			}

			std::string toolCommand = "himalaya ";
			if (!normalizedAccount.empty()) {
				toolCommand += "--account ";
				toolCommand += normalizedAccount;
				toolCommand += " ";
			}
			toolCommand += "template send";

			const std::string shellCommand =
				"cmd /C \"" +
				toolCommand +
				" < \"" +
				inputPath.string() +
				"\" 2>&1\"";

			int exitCode = -1;
			std::string commandOutput;
			const bool launched = RunCommandWithOutput(
				shellCommand,
				commandOutput,
				exitCode);
			std::error_code removeEc;
			std::filesystem::remove(inputPath, removeEc);

			if (!launched) {
				outErrorCode = "himalaya_launch_failed";
				outErrorMessage = "himalaya_launch_failed";
				return false;
			}

			outOutput = json::Trim(commandOutput);
			if (exitCode != 0) {
				outErrorCode = "himalaya_send_failed";
				outErrorMessage = outOutput.empty()
					? "himalaya_send_failed"
					: outOutput;
				return false;
			}

			outStatus = "sent";
			if (outOutput.empty()) {
				outOutput = "ok";
			}
			return true;
		}

		bool DeliverViaImapSmtpSkill(
			const std::string& recipient,
			const std::string& subject,
			const std::string& body,
			const std::string& account,
			std::string& outStatus,
			std::string& outOutput,
			std::string& outErrorCode,
			std::string& outErrorMessage) {
			outStatus.clear();
			outOutput.clear();
			outErrorCode.clear();
			outErrorMessage.clear();

			const std::string mode = NormalizeImapSmtpMode();
			if (mode == "mock_success") {
				outStatus = "sent";
				outOutput = "mock_success";
				return true;
			}

			if (mode == "mock_failure") {
				outErrorCode = "imap_smtp_send_failed";
				outErrorMessage = "mock_failure";
				return false;
			}

			if (!HasNodeBinary()) {
				outErrorCode = "node_cli_missing";
				outErrorMessage = "node_cli_missing";
				return false;
			}

			const std::string skillRoot = ResolveImapSmtpSkillRoot();
			if (skillRoot.empty() || !HasImapSmtpSkill()) {
				outErrorCode = "imap_smtp_skill_missing";
				outErrorMessage = "imap_smtp_skill_missing";
				return false;
			}

			std::string command = "cmd /C \"node ";
			command += QuoteArg(
				(std::filesystem::path(skillRoot) / "scripts" / "smtp.js").string());
			if (!json::Trim(account).empty()) {
				command += " --account ";
				command += QuoteArg(account);
			}
			command += " send";
			command += " --to ";
			command += QuoteArg(recipient);
			command += " --subject ";
			command += QuoteArg(subject);
			command += " --body ";
			command += QuoteArg(body);
			command += " 2>&1\"";

			int exitCode = -1;
			std::string commandOutput;
			const bool launched = RunCommandWithOutput(
				command,
				commandOutput,
				exitCode);
			if (!launched) {
				outErrorCode = "imap_smtp_launch_failed";
				outErrorMessage = "imap_smtp_launch_failed";
				return false;
			}

			outOutput = json::Trim(commandOutput);
			if (exitCode != 0) {
				outErrorCode = "imap_smtp_send_failed";
				outErrorMessage = outOutput.empty()
					? "imap_smtp_send_failed"
					: outOutput;
				return false;
			}

			outStatus = "sent";
			if (outOutput.empty()) {
				outOutput = "ok";
			}

			return true;
		}

		bool DeliverEmailWithFallback(
			const std::string& recipient,
			const std::string& subject,
			const std::string& body,
			const std::string& account,
			std::string& outBackend,
			std::string& outStatus,
			std::string& outOutput,
			std::string& outErrorCode,
			std::string& outErrorMessage) {
			outBackend.clear();
			outStatus.clear();
			outOutput.clear();
			outErrorCode.clear();
			outErrorMessage.clear();
			const auto deliveryPlan = ResolveEmailDeliveryResolverPlan();

			if (ParseBoolEnvOrDefault("BLAZECLAW_EMAIL_PREFLIGHT_ENABLED", false)) {
				const auto healthIndex = EmailScheduleExecutor::GetRuntimeHealthIndex(false);
				if (healthIndex.emailSendState == "unavailable") {
					outErrorCode = "email_delivery_unavailable_preflight";
					outErrorMessage = "email_send capability unavailable by preflight";
					return false;
				}
			}

			for (const auto& backend : deliveryPlan.backends) {
				std::uint32_t attempt = 0;
				while (attempt < deliveryPlan.retryMaxAttempts) {
					++attempt;

					std::string status;
					std::string output;
					std::string code;
					std::string message;

					bool delivered = false;
					if (backend == "himalaya") {
						delivered = DeliverViaHimalaya(
							recipient,
							subject,
							body,
							account,
							status,
							output,
							code,
							message);
					}
					else if (backend == "imap-smtp-email" ||
						backend == "imap_smtp_email") {
						delivered = DeliverViaImapSmtpSkill(
							recipient,
							subject,
							body,
							account,
							status,
							output,
							code,
							message);
					}

					if (delivered) {
						outBackend = backend;
						outStatus = status;
						outOutput = output;
						return true;
					}

					if (!code.empty()) {
						outErrorCode = code;
					}
					if (!message.empty()) {
						outErrorMessage = message;
					}

					const auto failureClass = ClassifyDeliveryFailure(code, message);
					const std::string action = ResolvePolicyActionForFailure(
						deliveryPlan,
						failureClass);

					const bool canRetrySameBackend =
						failureClass == EmailDeliveryFailureClass::ExecError &&
						action == "retry_then_continue" &&
						attempt < deliveryPlan.retryMaxAttempts;
					if (canRetrySameBackend) {
						if (deliveryPlan.retryDelayMs > 0) {
							std::this_thread::sleep_for(
								std::chrono::milliseconds(deliveryPlan.retryDelayMs));
						}
						continue;
					}

					if (action == "stop") {
						return false;
					}

					break;
				}
			}

			if (outErrorCode.empty()) {
				outErrorCode = "email_delivery_backends_exhausted";
			}
			if (outErrorMessage.empty()) {
				outErrorMessage = "email_delivery_backends_exhausted";
			}

			return false;
		}
	} // namespace

	GatewayToolRegistry::RuntimeToolExecutor EmailScheduleExecutor::Create() {
		return [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
			if (!argsJson.has_value()) {
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("missing_args", "missing_args"),
				};
			}

			std::string action;
			json::FindStringField(argsJson.value(), "action", action);
			if (action.empty()) {
				action = "prepare";
			}

			auto& store = SharedApprovalStore();

			if (action == "prepare") {
				std::string recipient;
				std::string subject;
				std::string body;
				std::string sendAt;
				std::string account;
				json::FindStringField(argsJson.value(), "to", recipient);
				json::FindStringField(argsJson.value(), "subject", subject);
				json::FindStringField(argsJson.value(), "body", body);
				json::FindStringField(argsJson.value(), "sendAt", sendAt);
				json::FindStringField(argsJson.value(), "account", account);

				if (json::Trim(recipient).empty() ||
					json::Trim(subject).empty() ||
					json::Trim(body).empty() ||
					json::Trim(sendAt).empty()) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = BuildErrorEnvelope(
							"to_subject_body_sendAt_required",
							"to_subject_body_sendAt_required"),
					};
				}

				std::uint64_t ttlMinutes = static_cast<std::uint64_t>(
					ParseUIntEnvOrDefault(
						"BLAZECLAW_EMAIL_APPROVAL_TOKEN_TTL_MINUTES",
						60));
				json::FindUInt64Field(argsJson.value(), "ttlMinutes", ttlMinutes);
				ttlMinutes = (std::max)(
					std::uint64_t{ 1 },
					(std::min)(ttlMinutes, std::uint64_t{ 1440 }));

				const auto nowEpochMs = CurrentEpochMs();
				const auto expiresAtEpochMs =
					nowEpochMs + ttlMinutes * std::uint64_t{ 60000 };
				const std::string token =
					"email-approval-" +
					std::to_string(nowEpochMs) +
					"-" +
					std::to_string(ttlMinutes);

				const std::string payload =
					"{\"to\":\"" +
					EscapeJson(recipient) +
					"\",\"subject\":\"" +
					EscapeJson(subject) +
					"\",\"body\":\"" +
					EscapeJson(body) +
					"\",\"sendAt\":\"" +
					EscapeJson(sendAt) +
					"\",\"account\":\"" +
					EscapeJson(account) +
					"\"}";

				const ApprovalSessionRecord session{
					.token = token,
					.type = "email.schedule",
					.payloadJson = payload,
					.createdAtEpochMs = nowEpochMs,
					.expiresAtEpochMs = expiresAtEpochMs,
				};

				if (!store.SaveSession(session)) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "error",
						.output = BuildErrorEnvelope(
							"approval_persist_failed",
							"approval_token_persist_failed"),
					};
				}

				store.PruneExpired(nowEpochMs);

				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = true,
					.status = "needs_approval",
					.output = BuildNeedsApprovalEnvelope(
						recipient,
						subject,
						sendAt,
						token,
						expiresAtEpochMs),
				};
			}

			if (action == "approve") {
				std::string token;
				bool approve = false;
				try {
					const auto parsedArgs = nlohmann::json::parse(argsJson.value());
					if (parsedArgs.contains("approvalToken") &&
						parsedArgs["approvalToken"].is_string()) {
						token = parsedArgs["approvalToken"].get<std::string>();
					}
					if (parsedArgs.contains("approve") &&
						parsedArgs["approve"].is_boolean()) {
						approve = parsedArgs["approve"].get<bool>();
					}
					else {
						throw std::runtime_error("approve_bool_required");
					}
				}
				catch (...) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = BuildErrorEnvelope(
							"approvalToken_and_approve_required",
							"approvalToken_and_approve_required"),
					};
				}

				if (json::Trim(token).empty()) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = BuildErrorEnvelope(
							"approvalToken_and_approve_required",
							"approvalToken_and_approve_required"),
					};
				}

				ApprovalSessionRecord session;
				const auto nowEpochMs = CurrentEpochMs();
				if (!store.IsTokenValid(token, nowEpochMs, &session)) {
					const auto existing = store.LoadSession(token);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = BuildErrorEnvelope(
							existing.has_value()
								? "approval_token_expired"
								: "approval_token_invalid",
							existing.has_value()
								? "approval_token_expired"
								: "approval_token_invalid"),
					};
				}

				if (session.type != "email.schedule") {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = BuildErrorEnvelope(
							"approval_token_orphaned",
							"approval_token_orphaned"),
					};
				}

				std::string recipient;
				std::string subject;
				std::string body;
				std::string sendAt;
				std::string account;
				json::FindStringField(session.payloadJson, "to", recipient);
				json::FindStringField(session.payloadJson, "subject", subject);
				json::FindStringField(session.payloadJson, "body", body);
				json::FindStringField(session.payloadJson, "sendAt", sendAt);
				json::FindStringField(session.payloadJson, "account", account);

				if (!approve) {
					store.RemoveToken(token);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "cancelled",
						.output = BuildResultEnvelope(
							false,
							false,
							recipient,
							subject,
							sendAt,
							"himalaya",
							"cancelled",
							"cancelled_by_user"),
					};
				}

				std::string transportBackend;
				std::string transportStatus;
				std::string transportOutput;
				std::string deliveryErrorCode;
				std::string deliveryErrorMessage;
				const bool delivered = DeliverEmailWithFallback(
					recipient,
					subject,
					body,
					account,
					transportBackend,
					transportStatus,
					transportOutput,
					deliveryErrorCode,
					deliveryErrorMessage);
				if (!delivered) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "error",
						.output = BuildErrorEnvelope(
							deliveryErrorCode,
							deliveryErrorMessage),
					};
				}

				store.RemoveToken(token);

				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = true,
					.status = "ok",
					.output = BuildResultEnvelope(
						true,
						delivered,
						recipient,
						subject,
						sendAt,
						transportBackend.empty() ? "himalaya" : transportBackend,
						transportStatus,
						transportOutput),
				};
			}

			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope(
					"action_prepare_or_approve_required",
					"action_prepare_or_approve_required"),
			};
			};
	}

	RuntimeHealthIndex EmailScheduleExecutor::GetRuntimeHealthIndex(
		const bool forceRefresh) {
		const std::uint64_t nowEpochMs = CurrentEpochMs();
		const std::uint64_t ttlMs = ResolvePreflightTtlMs();

		std::lock_guard<std::mutex> lock(HealthIndexCacheMutex());
		auto& cache = MutableHealthIndexCache();
		const bool cacheValid =
			cache.generatedAtEpochMs > 0 &&
			(nowEpochMs - cache.generatedAtEpochMs) <= ttlMs;

		if (!forceRefresh && cacheValid) {
			return cache;
		}

		try {
			cache = BuildRuntimeHealthIndexSnapshot(nowEpochMs, ttlMs);
		}
		catch (...) {
			cache = BuildFallbackHealthIndex(nowEpochMs, ttlMs);
		}

		return cache;
	}
} // namespace blazeclaw::gateway::executors
