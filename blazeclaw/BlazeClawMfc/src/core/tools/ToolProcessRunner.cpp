#include "pch.h"
#include "ToolProcessRunner.h"

#include <chrono>
#include <optional>

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

		std::string ToUtf8(const std::wstring& value)
		{
			if (value.empty())
			{
				return {};
			}

			const int needed = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (needed <= 0)
			{
				return {};
			}

			std::string output(static_cast<std::size_t>(needed), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				needed,
				nullptr,
				nullptr);
			return output;
		}

		std::optional<std::wstring> ReadEnvWide(const wchar_t* key)
		{
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 ||
				value == nullptr ||
				length == 0)
			{
				if (value != nullptr)
				{
					free(value);
				}
				return std::nullopt;
			}

			std::wstring result(value);
			free(value);
			return result;
		}

		std::optional<std::wstring> ResolveWithSearchPath(const wchar_t* executable)
		{
			DWORD required = SearchPathW(
				nullptr,
				executable,
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required == 0)
			{
				return std::nullopt;
			}

			std::vector<wchar_t> buffer(required + 1, L'\0');
			DWORD written = SearchPathW(
				nullptr,
				executable,
				nullptr,
				static_cast<DWORD>(buffer.size()),
				buffer.data(),
				nullptr);
			if (written == 0 || written >= buffer.size())
			{
				return std::nullopt;
			}

			return std::wstring(buffer.data(), written);
		}

#ifdef _WIN32
		std::optional<std::wstring> ProbeWellKnownWindowsNodeExecutable(std::string& outDetail)
		{
			outDetail.clear();
			static const wchar_t* kProgramRoots[] = {
				L"ProgramW6432",
				L"ProgramFiles",
				L"ProgramFiles(x86)",
			};
			for (const wchar_t* key : kProgramRoots) {
				const auto base = ReadEnvWide(key);
				if (!base.has_value()) {
					continue;
				}

				const std::filesystem::path candidate =
					std::filesystem::path(base.value()) / L"nodejs" / L"node.exe";
				if (std::filesystem::exists(candidate)) {
					outDetail =
						std::string("source=known_install key=") +
						ToUtf8(key) +
						" path=" +
						ToUtf8(candidate.wstring());
					return candidate.wstring();
				}
			}

			const auto localApp = ReadEnvWide(L"LOCALAPPDATA");
			if (localApp.has_value()) {
				const std::filesystem::path candidate =
					std::filesystem::path(localApp.value()) /
					L"Programs" /
					L"nodejs" /
					L"node.exe";
				if (std::filesystem::exists(candidate)) {
					outDetail =
						"source=known_install key=LOCALAPPDATA\\Programs\\nodejs path=" +
						ToUtf8(candidate.wstring());
					return candidate.wstring();
				}
			}

			return std::nullopt;
		}
#endif

		std::string Win32CreateProcessErrorSuffix(const unsigned long long lastError)
		{
			switch (lastError) {
			case 2ULL:
				return " win32Hint=ERROR_FILE_NOT_FOUND";
			case 3ULL:
				return " win32Hint=ERROR_PATH_NOT_FOUND";
			case 5ULL:
				return " win32Hint=ERROR_ACCESS_DENIED";
			case 193ULL:
				return " win32Hint=ERROR_BAD_EXE_FORMAT";
			case 740ULL:
				return " win32Hint=ERROR_ELEVATION_REQUIRED";
			case 206ULL:
				return " win32Hint=ERROR_FILENAME_EXCED_RANGE";
			case 267ULL:
				return " win32Hint=ERROR_DIRECTORY";
			case 87ULL:
				return " win32Hint=ERROR_INVALID_PARAMETER";
			default:
				return {};
			}
		}

		std::optional<std::wstring> ResolveNodeExecutable(
			std::string& outResolutionDetail)
		{
			outResolutionDetail.clear();

			const auto configuredPath = ReadEnvWide(L"BLAZECLAW_NODE_PATH");
			if (configuredPath.has_value())
			{
				const std::filesystem::path configured(configuredPath.value());
				if (configured.is_absolute() && std::filesystem::exists(configured))
				{
					outResolutionDetail =
						"source=BLAZECLAW_NODE_PATH path=" +
						ToUtf8(configuredPath.value());
					return configuredPath;
				}

				outResolutionDetail =
					"configured_path_invalid path=" +
					ToUtf8(configuredPath.value());
			}

			if (const auto found = ResolveWithSearchPath(L"node.exe"); found.has_value())
			{
				const std::string source = configuredPath.has_value()
					? "source=SearchPathW(after_configured_path)"
					: "source=SearchPathW";
				outResolutionDetail = source + " path=" + ToUtf8(found.value());
				return found;
			}

			if (const auto found = ResolveWithSearchPath(L"node"); found.has_value())
			{
				const std::string source = configuredPath.has_value()
					? "source=SearchPathW(after_configured_path)"
					: "source=SearchPathW";
				outResolutionDetail = source + " path=" + ToUtf8(found.value());
				return found;
			}

#ifdef _WIN32
			{
				std::string knownInstallDetail;
				if (const auto known = ProbeWellKnownWindowsNodeExecutable(knownInstallDetail);
					known.has_value()) {
					const std::string suffix =
						std::string("fallback=known_windows_install ") + knownInstallDetail;
					if (configuredPath.has_value()) {
						outResolutionDetail += " " + suffix;
					}
					else {
						outResolutionDetail = suffix;
					}
					return known;
				}
			}
#endif

			if (configuredPath.has_value())
			{
				outResolutionDetail += " fallback=SearchPathW_not_found";
			}
			else
			{
				outResolutionDetail = "source=SearchPathW_not_found";
			}

			return std::nullopt;
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
			result.commandLine = ToUtf8(BuildCommandLine(commandTokens));
			result.executablePath = commandTokens.empty()
				? std::string()
				: ToUtf8(commandTokens.front());
			if (workingDirectory != nullptr)
			{
				result.workingDirectory = ToUtf8(std::wstring(workingDirectory));
			}

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

			DWORD creationFlags = CREATE_NO_WINDOW;
			if (environment != nullptr) {
				// lpEnvironment is UTF-16; without this flag CreateProcessW often fails
				// with ERROR_INVALID_PARAMETER (87). See MSDN CREATE_UNICODE_ENVIRONMENT.
				creationFlags |= CREATE_UNICODE_ENVIRONMENT;
			}

			const BOOL created = CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				creationFlags,
				reinterpret_cast<LPVOID>(const_cast<wchar_t*>(environment)),
				workingDirectory,
				&startupInfo,
				&processInfo);

			CloseHandle(outputWrite);

			if (!created)
			{
				result.startupLastError = GetLastError();
				result.errorCode = "process_start_failed";
				result.errorMessage =
					"failed to start tool process lastError=" +
					std::to_string(static_cast<unsigned long long>(result.startupLastError)) +
					" executable=" +
					result.executablePath +
					" workingDir=" +
					(result.workingDirectory.empty() ? std::string("<null>") : result.workingDirectory) +
					Win32CreateProcessErrorSuffix(
						static_cast<unsigned long long>(result.startupLastError));
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
		std::string nodeResolution;
		const auto nodeExecutable = ResolveNodeExecutable(nodeResolution);
		if (!nodeExecutable.has_value())
		{
			ChildProcessResult result;
			result.started = false;
			result.errorCode = "node_not_found";
			result.errorMessage =
				"Node.js executable could not be resolved. " + nodeResolution;
			result.executablePath = "node";
			return result;
		}

		std::vector<std::wstring> commandTokens;
		commandTokens.push_back(nodeExecutable.value());
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

		// Inherit the parent environment (lpEnvironment=nullptr). A minimal custom block
		// (e.g. PYTHONUTF8) *replaces* the entire child env on Windows and drops SystemRoot,
		// PATH, etc., which often makes node.exe abort during InitializeOncePerProcessInternal
		// (exit 134, mangled C++ stderr from node.lib).
		auto result = ExecuteSkillProcess(
			commandTokens,
			timeoutMs,
			nullptr,
			workingDirectory);
		if (!result.started && !nodeResolution.empty())
		{
			result.errorMessage += " nodeResolution=" + nodeResolution;
		}
		return result;
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
