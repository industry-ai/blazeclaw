#include "pch.h"
#include "ToolProcessRunner.h"

#include <chrono>

namespace blazeclaw::core::tools {

namespace {

std::wstring ToWide(const std::string& value)
{
    if (value.empty())
    {
        return {};
    }

    const int needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (needed <= 0)
    {
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

void DrainPipeAvailable(HANDLE readPipe, std::string& output)
{
    if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE)
    {
        return;
    }

    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(
            readPipe,
            nullptr,
            0,
            nullptr,
            &available,
            nullptr) ||
            available == 0)
        {
            break;
        }

        char buffer[4096]{};
        const DWORD toRead =
            available > sizeof(buffer)
            ? static_cast<DWORD>(sizeof(buffer))
            : available;
        DWORD bytesRead = 0;
        if (!ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) ||
            bytesRead == 0)
        {
            break;
        }

        output.append(buffer, buffer + bytesRead);
    }
}

std::wstring QuoteCommandToken(const std::wstring& token)
{
    if (token.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return token;
    }

    std::wstring quoted = L"\"";
    for (const wchar_t ch : token)
    {
        if (ch == L'\"')
        {
            quoted += L"\\\"";
        }
        else
        {
            quoted.push_back(ch);
        }
    }
    quoted += L"\"";
    return quoted;
}

std::wstring BuildCommandLine(const std::vector<std::wstring>& tokens)
{
    std::wstring commandLine;
    for (std::size_t i = 0; i < tokens.size(); ++i)
    {
        if (i > 0)
        {
            commandLine += L" ";
        }

        commandLine += QuoteCommandToken(tokens[i]);
    }

    return commandLine;
}

std::string ReadPipeAll(HANDLE readPipe)
{
    std::string output;
    if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE)
    {
        return output;
    }

    char buffer[4096]{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) &&
        bytesRead > 0)
    {
        output.append(buffer, buffer + bytesRead);
    }

    return output;
}

ChildProcessResult ExecuteSkillProcess(
    const std::vector<std::wstring>& commandTokens,
    const std::uint64_t timeoutMs,
    LPCWSTR environment,
    LPCWSTR workingDirectory)
{
    ChildProcessResult result;

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    security.lpSecurityDescriptor = nullptr;

    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0))
    {
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
        reinterpret_cast<LPVOID>(const_cast<wchar_t*>(environment)),
        workingDirectory,
        &startupInfo,
        &processInfo);

    CloseHandle(outputWrite);

    if (!created)
    {
        result.errorCode = "process_start_failed";
        result.errorMessage = "failed to start tool process";
        result.output = ReadPipeAll(outputRead);
        CloseHandle(outputRead);
        return result;
    }

    result.started = true;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    DWORD waitResult = WAIT_TIMEOUT;
    for (;;)
    {
        waitResult = WaitForSingleObject(processInfo.hProcess, 50);
        DrainPipeAvailable(outputRead, result.output);

        if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_FAILED)
        {
            break;
        }

        if (std::chrono::steady_clock::now() >= deadline)
        {
            waitResult = WAIT_TIMEOUT;
            break;
        }
    }

    if (waitResult == WAIT_TIMEOUT)
    {
        TerminateProcess(processInfo.hProcess, 124);
        WaitForSingleObject(processInfo.hProcess, 5000);
        result.timedOut = true;
        result.errorCode = "deadline_exceeded";
        result.errorMessage = "tool process exceeded execution deadline";
    }
    else if (waitResult == WAIT_FAILED)
    {
        result.errorCode = "process_wait_failed";
        result.errorMessage = "failed to wait for tool process";
    }

    GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
    DrainPipeAvailable(outputRead, result.output);
    result.output += ReadPipeAll(outputRead);

    CloseHandle(outputRead);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return result;
}

} // namespace

ChildProcessResult ExecuteNodeSkillProcess(
    const std::filesystem::path& scriptPath,
    const std::vector<std::string>& cliArgs,
    const std::uint64_t timeoutMs)
{
    std::vector<std::wstring> commandTokens;
    commandTokens.push_back(L"node");
    commandTokens.push_back(scriptPath.wstring());
    for (const auto& arg : cliArgs)
    {
        commandTokens.push_back(ToWide(arg));
    }

    const std::wstring scriptWorkingDirW =
        scriptPath.has_parent_path()
        ? scriptPath.parent_path().wstring()
        : std::wstring();
    LPCWSTR workingDirectory =
        scriptWorkingDirW.empty()
        ? nullptr
        : scriptWorkingDirW.c_str();

    return ExecuteSkillProcess(
        commandTokens,
        timeoutMs,
        L"PYTHONUTF8=1\0PYTHONIOENCODING=utf-8\0\0",
        workingDirectory);
}

ChildProcessResult ExecutePythonSkillProcess(
    const std::filesystem::path& scriptPath,
    const std::vector<std::string>& cliArgs,
    const std::uint64_t timeoutMs)
{
    _wputenv_s(L"PYTHONUTF8", L"1");
    _wputenv_s(L"PYTHONIOENCODING", L"utf-8");

    std::vector<std::wstring> commandTokens;
    commandTokens.push_back(L"python");
    commandTokens.push_back(L"-X");
    commandTokens.push_back(L"utf8");
    commandTokens.push_back(scriptPath.wstring());
    for (const auto& arg : cliArgs)
    {
        commandTokens.push_back(ToWide(arg));
    }

    return ExecuteSkillProcess(
        commandTokens,
        timeoutMs,
        nullptr,
        nullptr);
}

} // namespace blazeclaw::core::tools
