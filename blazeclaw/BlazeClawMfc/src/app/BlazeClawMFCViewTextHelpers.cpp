#include "pch.h"
#include "BlazeClawMFCViewTextHelpers.h"

#include "../gateway/GatewayJsonUtils.h"

namespace blazeclaw::app::view_helpers {

	std::string ToNarrowUtf8(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int sizeNeeded = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (sizeNeeded <= 0)
		{
			std::string fallback;
			fallback.reserve(value.size());
			for (const wchar_t ch : value)
			{
				fallback.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return fallback;
		}

		std::string output(sizeNeeded, '\0');
		const int written = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			sizeNeeded,
			nullptr,
			nullptr);
		if (written <= 0)
		{
			return {};
		}

		return output;
	}

	std::string BuildSkillPathFromDeltaText(const std::string& text)
	{
		const std::string trimmed = blazeclaw::gateway::json::Trim(text);
		if (trimmed.empty())
		{
			return {};
		}

		const bool isStart =
			trimmed.rfind("tools.execute.start tool=", 0) == 0;
		const bool isResult =
			trimmed.rfind("tools.execute.result tool=", 0) == 0;
		if (!isStart && !isResult)
		{
			return {};
		}

		const std::string toolToken = " tool=";
		const std::size_t toolPos = trimmed.find(toolToken);
		if (toolPos == std::string::npos)
		{
			return {};
		}

		const std::size_t toolStart = toolPos + toolToken.size();
		std::size_t toolEnd = trimmed.find(' ', toolStart);
		if (toolEnd == std::string::npos)
		{
			toolEnd = trimmed.size();
		}

		const std::string tool = trimmed.substr(toolStart, toolEnd - toolStart);
		if (tool.empty())
		{
			return {};
		}

		std::string status = isStart ? "requested" : "ok";
		const std::string statusToken = " status=";
		const std::size_t statusPos = trimmed.find(statusToken);
		if (statusPos != std::string::npos)
		{
			const std::size_t statusStart = statusPos + statusToken.size();
			std::size_t statusEnd = trimmed.find(' ', statusStart);
			if (statusEnd == std::string::npos)
			{
				statusEnd = trimmed.size();
			}

			const std::string parsed = trimmed.substr(
				statusStart,
				statusEnd - statusStart);
			if (!parsed.empty())
			{
				status = parsed;
			}
		}

		std::string line = "[SkillPath] " + tool + " [" + status + "]";
		const std::string errorToken = " errorCode=";
		const std::size_t errorPos = trimmed.find(errorToken);
		if (errorPos != std::string::npos)
		{
			const std::string errorCode = trimmed.substr(
				errorPos + errorToken.size());
			if (!errorCode.empty())
			{
				line += " errorCode=" + errorCode;
			}
		}

		const std::string msgToken = " errorMessage=";
		const std::size_t msgPos = trimmed.find(msgToken);
		if (msgPos != std::string::npos)
		{
			std::size_t msgStart = msgPos + msgToken.size();
			std::size_t msgEnd = trimmed.find(' ', msgStart);
			if (msgEnd == std::string::npos)
			{
				msgEnd = trimmed.size();
			}

			const std::string message = trimmed.substr(msgStart, msgEnd - msgStart);
			if (!message.empty())
			{
				line += " errorMessage=" + message;
			}
		}

		return line;
	}

} // namespace blazeclaw::app::view_helpers
