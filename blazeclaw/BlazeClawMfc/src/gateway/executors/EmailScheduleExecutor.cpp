#include "pch.h"
#include "EmailScheduleExecutor.h"

#include "../ApprovalTokenStore.h"
#include "../GatewayJsonUtils.h"
#include "../GatewayPersistencePaths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway::executors {
	namespace {

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
				store.Initialize(ResolveGatewayStateFilePath("approvals.json").string());
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

		std::string NormalizeMode() {
			const auto mode = ToLowerCopy(json::Trim(ReadEnvVar("BLAZECLAW_EMAIL_DELIVERY_MODE")));
			if (mode == "mock_success" || mode == "mock_failure") {
				return mode;
			}

			return "auto";
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

				std::uint64_t ttlMinutes = 60;
				json::FindUInt64Field(argsJson.value(), "ttlMinutes", ttlMinutes);
				ttlMinutes = (std::max)(std::uint64_t{ 1 }, (std::min)(ttlMinutes, std::uint64_t{ 1440 }));

				const auto nowEpochMs = CurrentEpochMs();
				const auto expiresAtEpochMs = nowEpochMs + ttlMinutes * std::uint64_t{ 60000 };
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
							existing.has_value() ? "approval_token_expired" : "approval_token_invalid",
							existing.has_value() ? "approval_token_expired" : "approval_token_invalid"),
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

				store.RemoveToken(token);

				if (!approve) {
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

				std::string transportStatus;
				std::string transportOutput;
				std::string deliveryErrorCode;
				std::string deliveryErrorMessage;
				const bool delivered = DeliverViaHimalaya(
					recipient,
					subject,
					body,
					account,
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
						"himalaya",
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

} // namespace blazeclaw::gateway::executors
