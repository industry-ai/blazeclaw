#include "pch.h"
#include "LobsterExecutor.h"

#include "../GatewayJsonUtils.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <windows.h>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway::executors {
	namespace {
		using ProcessRunOutcome = LobsterExecutor::ProcessRunOutcome;
		using ProcessRunResult = LobsterExecutor::ProcessRunResult;
		using Settings = LobsterExecutor::Settings;

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

		std::optional<std::string> ParseLastJsonSuffix(const std::string& text) {
			std::string trimmed = text;
			while (!trimmed.empty() &&
				isspace(static_cast<unsigned char>(trimmed.back()))) {
				trimmed.pop_back();
			}

			for (std::size_t index = trimmed.size(); index > 0; --index) {
				const char marker = trimmed[index - 1];
				if (marker != '{' && marker != '[') {
					continue;
				}

				const std::string candidate = trimmed.substr(index - 1);
				auto parsed = nlohmann::json::parse(candidate, nullptr, false);
				if (parsed.is_discarded()) {
					continue;
				}

				if (!parsed.is_object() && !parsed.is_array()) {
					continue;
				}

				return candidate;
			}

			return std::nullopt;
		}

		bool IsJsonObjectOrArray(const std::string& raw) {
			const std::string trimmed = json::Trim(raw);
			if (trimmed.empty()) {
				return false;
			}

			return (trimmed.front() == '{' && trimmed.back() == '}') ||
				(trimmed.front() == '[' && trimmed.back() == ']');
		}

		bool IsNormalizedLobsterEnvelope(const std::string& raw) {
			const std::string trimmed = json::Trim(raw);
			if (!json::IsJsonObjectShape(trimmed)) {
				return false;
			}

			return trimmed.find("\"protocolVersion\"") != std::string::npos &&
				trimmed.find("\"status\"") != std::string::npos &&
				trimmed.find("\"requiresApproval\"") != std::string::npos;
		}

		std::string ResolveAllowedRoot(const std::vector<std::string>& roots) {
			if (!roots.empty()) {
				return roots.front();
			}

			try {
				return std::filesystem::current_path().string();
			}
			catch (...) {
				return {};
			}
		}

		bool IsPathWithinRoot(
			const std::filesystem::path& candidate,
			const std::filesystem::path& root) {
			std::error_code ec;
			const auto canonicalCandidate =
				std::filesystem::weakly_canonical(candidate, ec);
			if (ec) {
				return false;
			}

			ec.clear();
			const auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
			if (ec) {
				return false;
			}

			auto candidateText = canonicalCandidate.string();
			auto rootText = canonicalRoot.string();
			if (!rootText.empty() &&
				rootText.back() != std::filesystem::path::preferred_separator) {
				rootText.push_back(std::filesystem::path::preferred_separator);
			}

			return candidateText.rfind(rootText, 0) == 0;
		}

		bool ValidateCwd(
			const std::string& cwd,
			const Settings& settings,
			std::string& failureCode,
			std::string& failureMessage) {
			if (!settings.enforceWorkspaceCwd || json::Trim(cwd).empty()) {
				return true;
			}

			const auto cwdPath = std::filesystem::path(cwd);
			for (const auto& root : settings.allowedWorkspaceRoots) {
				if (!root.empty() &&
					IsPathWithinRoot(cwdPath, std::filesystem::path(root))) {
					return true;
				}
			}

			if (settings.allowedWorkspaceRoots.empty()) {
				const auto fallbackRoot = ResolveAllowedRoot({});
				if (!fallbackRoot.empty() &&
					IsPathWithinRoot(cwdPath, std::filesystem::path(fallbackRoot))) {
					return true;
				}
			}

			failureCode = "invalid_cwd_outside_workspace";
			failureMessage = cwd;
			return false;
		}

		std::string BuildErrorEnvelope(
			const std::string& code,
			const std::string& message,
			const std::optional<std::string>& rawOutput = std::nullopt) {
			std::string outputJson = "[]";
			if (rawOutput.has_value() && !json::Trim(rawOutput.value()).empty()) {
				const std::string truncated =
					rawOutput->size() > 512
					? rawOutput->substr(0, 512)
					: rawOutput.value();
				outputJson = "[{\"raw\":\"" + EscapeJson(truncated) + "\"}]";
			}

			return "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\","
				"\"output\":" + outputJson +
				",\"requiresApproval\":null,\"error\":{\"code\":\"" +
				EscapeJson(code) +
				"\",\"message\":\"" +
				EscapeJson(message) +
				"\"}}";
		}

		std::string BuildWrappedSuccessEnvelope(
			const std::string& payload,
			const std::string& status) {
			if (IsJsonObjectOrArray(payload)) {
				return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"" +
					EscapeJson(status) +
					"\",\"output\":[{\"executorPayload\":" +
					payload +
					"}],\"requiresApproval\":null}";
			}

			return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"" +
				EscapeJson(status) +
				"\",\"output\":[{\"text\":\"" +
				EscapeJson(payload) +
				"\"}],\"requiresApproval\":null}";
		}

		ProcessRunResult RunProcessAndCapture(
			const std::string& execPath,
			const std::vector<std::string>& argv,
			const unsigned long timeoutMs,
			const std::size_t maxStdoutBytes) {
			std::ostringstream cmd;
			cmd << '"' << execPath << '"';
			for (const auto& arg : argv) {
				cmd << ' ';
				if (arg.find(' ') != std::string::npos) {
					cmd << '"' << arg << '"';
				}
				else {
					cmd << arg;
				}
			}

			SECURITY_ATTRIBUTES saAttr{};
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = NULL;

			HANDLE hStdOutRead = NULL;
			HANDLE hStdOutWrite = NULL;
			if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
				return ProcessRunResult{ ProcessRunOutcome::SpawnFailed, {}, -1 };
			}
			if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(hStdOutRead);
				CloseHandle(hStdOutWrite);
				return ProcessRunResult{ ProcessRunOutcome::SpawnFailed, {}, -1 };
			}

			STARTUPINFOA siStartInfo{};
			PROCESS_INFORMATION piProcInfo{};
			siStartInfo.cb = sizeof(STARTUPINFOA);
			siStartInfo.hStdError = hStdOutWrite;
			siStartInfo.hStdOutput = hStdOutWrite;
			siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

			std::string cmdStr = cmd.str();
			std::unique_ptr<char[]> cmdLine(new char[cmdStr.size() + 1]);
			std::copy(cmdStr.begin(), cmdStr.end(), cmdLine.get());
			cmdLine[cmdStr.size()] = '\0';

			BOOL created = CreateProcessA(
				execPath.c_str(),
				cmdLine.get(),
				NULL,
				NULL,
				TRUE,
				CREATE_NO_WINDOW,
				NULL,
				NULL,
				&siStartInfo,
				&piProcInfo);

			CloseHandle(hStdOutWrite);
			if (!created) {
				CloseHandle(hStdOutRead);
				return ProcessRunResult{ ProcessRunOutcome::SpawnFailed, {}, -1 };
			}

			DWORD start = GetTickCount();
			std::string collected;
			collected.reserve(4096);
			char buffer[4096];
			DWORD bytesRead = 0;
			bool timedOut = false;
			bool outputLimitExceeded = false;

			while (true) {
				DWORD now = GetTickCount();
				DWORD elapsed = now - start;

				if (WaitForSingleObject(piProcInfo.hProcess, 0) == WAIT_OBJECT_0) {
					while (true) {
						BOOL ok = ReadFile(
							hStdOutRead,
							buffer,
							sizeof(buffer),
							&bytesRead,
							NULL);
						if (!ok || bytesRead == 0) {
							break;
						}

						collected.append(buffer, buffer + bytesRead);
						if (collected.size() > maxStdoutBytes) {
							outputLimitExceeded = true;
							TerminateProcess(piProcInfo.hProcess, 1);
							break;
						}
					}
					break;
				}

				BOOL ok = PeekNamedPipe(
					hStdOutRead,
					NULL,
					0,
					NULL,
					&bytesRead,
					NULL);
				if (ok && bytesRead > 0) {
					DWORD toRead = (std::min)(bytesRead, static_cast<DWORD>(sizeof(buffer)));
					if (ReadFile(hStdOutRead, buffer, toRead, &bytesRead, NULL) &&
						bytesRead > 0) {
						collected.append(buffer, buffer + bytesRead);
						if (collected.size() > maxStdoutBytes) {
							outputLimitExceeded = true;
							TerminateProcess(piProcInfo.hProcess, 1);
							break;
						}
					}
				}

				if (elapsed > timeoutMs) {
					timedOut = true;
					TerminateProcess(piProcInfo.hProcess, 1);
					break;
				}

				Sleep(20);
			}

			DWORD code = 0;
			GetExitCodeProcess(piProcInfo.hProcess, &code);

			CloseHandle(hStdOutRead);
			CloseHandle(piProcInfo.hProcess);
			CloseHandle(piProcInfo.hThread);

			if (timedOut) {
				return ProcessRunResult{ ProcessRunOutcome::TimedOut, std::move(collected), static_cast<int>(code) };
			}
			if (outputLimitExceeded) {
				return ProcessRunResult{ ProcessRunOutcome::OutputLimitExceeded, std::move(collected), static_cast<int>(code) };
			}

			return ProcessRunResult{ ProcessRunOutcome::Completed, std::move(collected), static_cast<int>(code) };
		}

		ToolExecuteResult BuildInvalidArgsResult(
			const std::string& requestedTool,
			const std::string& code,
			const std::string& message = {}) {
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope(
					code,
					message.empty() ? code : message),
			};
		}
	} // namespace

    GatewayToolRegistry::RuntimeToolExecutor LobsterExecutor::Create(
		const std::string& defaultExecPath,
		Settings settings) {
		return [defaultExecPath, settings = std::move(settings)](
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson) -> ToolExecuteResult {
			std::string action;
			if (argsJson.has_value()) {
				json::FindStringField(argsJson.value(), "action", action);
			}

			if (action.empty()) {
				return BuildInvalidArgsResult(requestedTool, "action_required");
			}

			std::vector<std::string> argv;
           std::string cwd;
			try {
				if (action == "run") {
					std::string pipeline;
                  if (!argsJson.has_value()) {
						return BuildInvalidArgsResult(requestedTool, "missing_args");
					}
					json::FindStringField(argsJson.value(), "pipeline", pipeline);
					if (json::Trim(pipeline).empty()) {
                      return BuildInvalidArgsResult(requestedTool, "pipeline_required");
					}
					if (pipeline.size() > settings.maxArgumentChars) {
						return BuildInvalidArgsResult(
							requestedTool,
							"pipeline_too_large");
					}

					json::FindStringField(argsJson.value(), "cwd", cwd);

					argv.push_back("run");
					argv.push_back("--mode");
					argv.push_back("tool");
					argv.push_back(pipeline);
				}
				else if (action == "resume") {
					std::string token;
					bool approve = false;
                    if (!argsJson.has_value()) {
						return BuildInvalidArgsResult(requestedTool, "missing_args");
					}
					json::FindStringField(argsJson.value(), "token", token);
					if (token.empty()) {
                     return BuildInvalidArgsResult(requestedTool, "token_required");
					}
					if (token.size() > settings.maxArgumentChars) {
						return BuildInvalidArgsResult(
							requestedTool,
							"token_too_large");
					}
					if (!json::FindBoolField(argsJson.value(), "approve", approve)) {
                       return BuildInvalidArgsResult(requestedTool, "approve_required");
					}

					json::FindStringField(argsJson.value(), "cwd", cwd);

					argv.push_back("resume");
					argv.push_back("--token");
					argv.push_back(token);
					argv.push_back("--approve");
					argv.push_back(approve ? "yes" : "no");
				}
				else {
                    return BuildInvalidArgsResult(requestedTool, "action_required");
				}
			}
			catch (const std::bad_optional_access&) {
               return BuildInvalidArgsResult(requestedTool, "missing_args");
			}

			std::string cwdFailureCode;
			std::string cwdFailureMessage;
			if (!ValidateCwd(cwd, settings, cwdFailureCode, cwdFailureMessage)) {
				return BuildInvalidArgsResult(
					requestedTool,
					cwdFailureCode,
					cwdFailureMessage);
			}

			if (!json::Trim(cwd).empty()) {
				argv.push_back("--cwd");
				argv.push_back(cwd);
			}

			const std::string execPath = defaultExecPath;
          const auto& runner = settings.processRunner
				? settings.processRunner
				: ProcessRunner(RunProcessAndCapture);

			const auto processResult = runner(
				execPath,
				argv,
				settings.timeoutMs,
				settings.maxStdoutBytes);

         if (processResult.outcome == ProcessRunOutcome::SpawnFailed) {
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"executor_spawn_failed",
						execPath),
				};
			}

			if (processResult.outcome == ProcessRunOutcome::TimedOut) {
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "timeout",
					.output = BuildErrorEnvelope(
						"timeout",
						"executor_timeout",
						processResult.stdoutText),
				};
			}

			if (processResult.outcome == ProcessRunOutcome::OutputLimitExceeded) {
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"output_limit_exceeded",
						"executor_output_limit_exceeded",
						processResult.stdoutText),
				};
			}

            const auto parsed = ParseLastJsonSuffix(processResult.stdoutText);
			if (!parsed.has_value()) {
                return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"invalid_executor_output",
						"json_suffix_not_found",
						processResult.stdoutText),
				};
			}

			if (processResult.exitCode != 0) {
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"executor_nonzero_exit",
						"exit_code=" + std::to_string(processResult.exitCode),
						parsed.value()),
				};
			}

            std::string status = "ok";
			json::FindStringField(parsed.value(), "status", status);

			const std::string output = IsNormalizedLobsterEnvelope(parsed.value())
				? parsed.value()
				: BuildWrappedSuccessEnvelope(parsed.value(), status);

			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = true,
				.status = status,
				.output = output,
			};
			};
	}

} // namespace blazeclaw::gateway::executors

