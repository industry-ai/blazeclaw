#include "pch.h"
#include "StartupPolicyResolver.h"

#include <filesystem>
#include <fstream>
#include <Windows.h>

namespace blazeclaw::core::bootstrap {

	void StartupPolicyResolver::AppendStartupTrace(const char* stage) const
	{
		if (stage == nullptr || *stage == '\0')
		{
			return;
		}

		wchar_t tempPath[MAX_PATH]{};
		const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
		std::filesystem::path logPath;
		if (tempLength > 0 && tempLength < MAX_PATH)
		{
			logPath = std::filesystem::path(tempPath) / L"BlazeClaw.startup.trace.log";
		}
		else
		{
			logPath = std::filesystem::current_path() / L"BlazeClaw.startup.trace.log";
		}

		std::ofstream output(logPath, std::ios::app);
		if (!output.is_open())
		{
			return;
		}

		output
			<< "pid=" << static_cast<unsigned long>(GetCurrentProcessId())
			<< " tick=" << static_cast<unsigned long long>(GetTickCount64())
			<< " stage=" << stage
			<< "\n";
	}

} // namespace blazeclaw::core::bootstrap
