#include "pch.h"
#include "LobsterExecutor.h"

#include "../GatewayJsonUtils.h"
#include <windows.h>
#include <sstream>
#include <memory>
#include <algorithm>

namespace blazeclaw::gateway::executors {

	static std::optional<std::string> ParseLastJsonSuffix(const std::string& text) {
		std::string trimmed = text;
		while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
		auto pos = trimmed.find_last_of("[{\n");
		if (pos == std::string::npos) return std::nullopt;
		const std::string candidate = trimmed.substr(pos);
		if (candidate.empty() || (candidate.front() != '{' && candidate.front() != '[')) return std::nullopt;
		return candidate;
	}

	static bool RunProcessAndCapture(const std::string& execPath, const std::vector<std::string>& argv, std::string& outStdout, int& outExitCode, unsigned long timeoutMs, std::size_t maxStdoutBytes) {
		std::ostringstream cmd;
		cmd << '"' << execPath << '"';
		for (const auto& a : argv) {
			cmd << ' ';
			if (a.find(' ') != std::string::npos) {
				cmd << '"' << a << '"';
			}
			else {
				cmd << a;
			}
		}

		SECURITY_ATTRIBUTES saAttr{};
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		HANDLE hStdOutRead = NULL;
		HANDLE hStdOutWrite = NULL;
		if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
			return false;
		}
		if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
			CloseHandle(hStdOutRead);
			CloseHandle(hStdOutWrite);
			return false;
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
			return false;
		}

		DWORD start = GetTickCount();
		std::string collected;
		collected.reserve(4096);
		char buffer[4096];
		DWORD bytesRead = 0;
		bool done = false;
		while (!done) {
			DWORD now = GetTickCount();
			DWORD elapsed = now - start;
			if (WaitForSingleObject(piProcInfo.hProcess, 0) == WAIT_OBJECT_0) {
				while (true) {
					BOOL ok = ReadFile(hStdOutRead, buffer, sizeof(buffer), &bytesRead, NULL);
					if (!ok || bytesRead == 0) break;
					collected.append(buffer, buffer + bytesRead);
					if (collected.size() > maxStdoutBytes) {
						TerminateProcess(piProcInfo.hProcess, 1);
						break;
					}
				}
				done = true;
				break;
			}

			BOOL ok = PeekNamedPipe(hStdOutRead, NULL, 0, NULL, &bytesRead, NULL);
			if (ok && bytesRead > 0) {
				DWORD toRead = (std::min)(bytesRead, static_cast<DWORD>(sizeof(buffer)));
				if (ReadFile(hStdOutRead, buffer, toRead, &bytesRead, NULL) && bytesRead > 0) {
					collected.append(buffer, buffer + bytesRead);
					if (collected.size() > maxStdoutBytes) {
						TerminateProcess(piProcInfo.hProcess, 1);
						break;
					}
				}
			}

			if (elapsed > timeoutMs) {
				TerminateProcess(piProcInfo.hProcess, 1);
				done = true;
				break;
			}

			Sleep(20);
		}

		DWORD code = 0;
		GetExitCodeProcess(piProcInfo.hProcess, &code);
		outExitCode = static_cast<int>(code);
		outStdout = std::move(collected);

		CloseHandle(hStdOutRead);
		CloseHandle(piProcInfo.hProcess);
		CloseHandle(piProcInfo.hThread);

		return true;
	}

	GatewayToolRegistry::RuntimeToolExecutor LobsterExecutor::Create(const std::string& defaultExecPath) {
		return [defaultExecPath](const std::string& requestedTool, const std::optional<std::string>& argsJson) -> ToolExecuteResult {
			std::string action;
			if (argsJson.has_value()) {
				json::FindStringField(argsJson.value(), "action", action);
			}

			std::vector<std::string> argv;
			try {
				if (action == "run") {
					std::string pipeline;
					json::FindStringField(argsJson.value(), "pipeline", pipeline);
					if (json::Trim(pipeline).empty()) {
						return ToolExecuteResult{ requestedTool, false, "invalid_args", "pipeline_required" };
					}
					argv.push_back("run");
					argv.push_back("--mode");
					argv.push_back("tool");
					argv.push_back(pipeline);
				}
				else if (action == "resume") {
					std::string token;
					bool approve = false;
					json::FindStringField(argsJson.value(), "token", token);
					if (token.empty()) {
						return ToolExecuteResult{ requestedTool, false, "invalid_args", "token_required" };
					}
					if (!json::FindBoolField(argsJson.value(), "approve", approve)) {
						return ToolExecuteResult{ requestedTool, false, "invalid_args", "approve_required" };
					}
					argv.push_back("resume");
					argv.push_back("--token");
					argv.push_back(token);
					argv.push_back("--approve");
					argv.push_back(approve ? "yes" : "no");
				}
				else {
					return ToolExecuteResult{ requestedTool, false, "invalid_args", "action_required" };
				}
			}
			catch (const std::bad_optional_access&) {
				return ToolExecuteResult{ requestedTool, false, "invalid_args", "missing_args" };
			}

			const std::string execPath = defaultExecPath;
			const unsigned long timeoutMs = 20000;
			const std::size_t maxStdoutBytes = 512000;

			std::string procStdout;
			int exitCode = -1;
			if (!RunProcessAndCapture(execPath, argv, procStdout, exitCode, timeoutMs, maxStdoutBytes)) {
				return ToolExecuteResult{ requestedTool, false, "error", "executor_spawn_failed" };
			}

			const auto parsed = ParseLastJsonSuffix(procStdout);
			if (!parsed.has_value()) {
				// return raw stdout as output for debugging
				return ToolExecuteResult{ requestedTool, true, exitCode == 0 ? "ok" : "error", procStdout };
			}

			return ToolExecuteResult{ requestedTool, true, exitCode == 0 ? "ok" : "error", parsed.value() };
			};
	}

} // namespace blazeclaw::gateway::executors

