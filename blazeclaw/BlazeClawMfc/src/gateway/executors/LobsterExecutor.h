#pragma once

#include "../GatewayToolRegistry.h"

#include <string>
#include <optional>
#include <vector>
#include <sstream>
#include <windows.h>
#include <memory>
#include <algorithm>
#include "../GatewayJsonUtils.h"

namespace blazeclaw::gateway::executors {

class LobsterExecutor {
public:
    // Create a runtime executor callable matching GatewayToolRegistry::RuntimeToolExecutor
    // The returned callable captures any configuration needed (e.g., default exec path).
    static inline GatewayToolRegistry::RuntimeToolExecutor Create(const std::string& defaultExecPath) {
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
                        return ToolExecuteResult{requestedTool, false, "invalid_args", "pipeline_required"};
                    }
                    argv.push_back("run");
                    argv.push_back("--mode");
                    argv.push_back("tool");
                    argv.push_back(pipeline);
                } else if (action == "resume") {
                    std::string token;
                    bool approve = false;
                    json::FindStringField(argsJson.value(), "token", token);
                    if (token.empty()) {
                        return ToolExecuteResult{requestedTool, false, "invalid_args", "token_required"};
                    }
                    if (!json::FindBoolField(argsJson.value(), "approve", approve)) {
                        return ToolExecuteResult{requestedTool, false, "invalid_args", "approve_required"};
                    }
                    argv.push_back("resume");
                    argv.push_back("--token");
                    argv.push_back(token);
                    argv.push_back("--approve");
                    argv.push_back(approve ? "yes" : "no");
                } else {
                    return ToolExecuteResult{requestedTool, false, "invalid_args", "action_required"};
                }
            } catch (const std::bad_optional_access&) {
                return ToolExecuteResult{requestedTool, false, "invalid_args", "missing_args"};
            }

            const std::string execPath = defaultExecPath;
            const unsigned long timeoutMs = 20000;
            const std::size_t maxStdoutBytes = 512000;

            // build command line
            std::ostringstream cmd;
            cmd << '"' << execPath << '"';
            for (const auto& a : argv) {
                cmd << ' ';
                if (a.find(' ') != std::string::npos) {
                    cmd << '"' << a << '"';
                } else {
                    cmd << a;
                }
            }

            // create pipes and launch process
            SECURITY_ATTRIBUTES saAttr{};
            saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
            saAttr.bInheritHandle = TRUE;
            saAttr.lpSecurityDescriptor = NULL;

            HANDLE hStdOutRead = NULL;
            HANDLE hStdOutWrite = NULL;
            if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
                return ToolExecuteResult{requestedTool, false, "error", "executor_spawn_failed"};
            }
            if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
                CloseHandle(hStdOutRead);
                CloseHandle(hStdOutWrite);
                return ToolExecuteResult{requestedTool, false, "error", "executor_spawn_failed"};
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
                return ToolExecuteResult{requestedTool, false, "error", "executor_spawn_failed"};
            }

            // Read stdout
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
            const int exitCode = static_cast<int>(code);
            const std::string procStdout = std::move(collected);

            CloseHandle(hStdOutRead);
            CloseHandle(piProcInfo.hProcess);
            CloseHandle(piProcInfo.hThread);

            // Parse last JSON-like suffix
            std::string trimmed = procStdout;
            while (!trimmed.empty() && isspace(static_cast<unsigned char>(trimmed.back()))) trimmed.pop_back();
            auto pos = trimmed.find_last_of("[{\n");
            if (pos == std::string::npos) {
                return ToolExecuteResult{requestedTool, exitCode == 0, exitCode == 0 ? "ok" : "error", procStdout};
            }
            const std::string candidate = trimmed.substr(pos);
            if (candidate.empty() || (candidate.front() != '{' && candidate.front() != '[')) {
                return ToolExecuteResult{requestedTool, exitCode == 0, exitCode == 0 ? "ok" : "error", procStdout};
            }

            return ToolExecuteResult{requestedTool, exitCode == 0, exitCode == 0 ? "ok" : "error", candidate};
        };
    }
};

} // namespace blazeclaw::gateway::executors
