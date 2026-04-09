#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <Windows.h>

namespace blazeclaw::core::tools {

struct ChildProcessResult {
    bool started = false;
    bool timedOut = false;
    DWORD exitCode = static_cast<DWORD>(-1);
    std::string output;
    std::string errorCode;
    std::string errorMessage;
};

ChildProcessResult ExecuteNodeSkillProcess(
    const std::filesystem::path& scriptPath,
    const std::vector<std::string>& cliArgs,
    std::uint64_t timeoutMs);

ChildProcessResult ExecutePythonSkillProcess(
    const std::filesystem::path& scriptPath,
    const std::vector<std::string>& cliArgs,
    std::uint64_t timeoutMs);

} // namespace blazeclaw::core::tools
