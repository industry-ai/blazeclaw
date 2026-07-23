#include "pch.h"
#include "BlazeClawMFCViewTextHelpers.h"

#include "../gateway/GatewayJsonUtils.h"

#include <array>
#include <cctype>

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

	std::vector<std::string> SplitTopLevelJsonObjects(const std::string& arrayJson)
	{
		std::vector<std::string> objects;
		const std::string trimmed = blazeclaw::gateway::json::Trim(arrayJson);
		if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
		{
			return objects;
		}

		bool inString = false;
		int depth = 0;
		std::size_t start = std::string::npos;
		for (std::size_t i = 0; i < trimmed.size(); ++i)
		{
			const char ch = trimmed[i];
			if (inString)
			{
				if (ch == '\\')
				{
					++i;
					continue;
				}

				if (ch == '"')
				{
					inString = false;
				}
				continue;
			}

			if (ch == '"')
			{
				inString = true;
				continue;
			}

			if (ch == '{')
			{
				if (depth == 0)
				{
					start = i;
				}
				++depth;
				continue;
			}

			if (ch == '}')
			{
				--depth;
				if (depth == 0 && start != std::string::npos)
				{
					objects.push_back(trimmed.substr(start, (i - start) + 1));
					start = std::string::npos;
				}
			}
		}

		return objects;
	}

	std::string NormalizeSkillKeyForPath(const std::string& skillKey)
	{
		std::string normalized;
		normalized.reserve(skillKey.size());
		for (const char ch : skillKey)
		{
			if (ch == '_')
			{
				normalized.push_back('-');
				continue;
			}

			normalized.push_back(static_cast<char>(
				std::tolower(static_cast<unsigned char>(ch))));
		}

		return normalized;
	}

	std::optional<std::filesystem::path> FindEmailConfigHtml(
		const std::filesystem::path& start)
	{
		std::filesystem::path cursor = start;
		while (!cursor.empty())
		{
			const auto configHtml =
				cursor /
				L"blazeclaw" /
				L"skills" /
				L"imap-smtp-email" /
				L"config.html";
			if (std::filesystem::exists(configHtml))
			{
				return configHtml;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

	std::optional<std::filesystem::path> FindSkillConfigHtml(
		const std::filesystem::path& start,
		const std::string& skillKey)
	{
		const std::string normalizedSkillKey = NormalizeSkillKeyForPath(skillKey);
		if (normalizedSkillKey.empty())
		{
			return std::nullopt;
		}

		const std::array<std::filesystem::path, 3> kSkillRootSuffixes = {
			std::filesystem::path(L"blazeclaw") / L"skills-bundled",
			std::filesystem::path(L"blazeclaw") / L"skills",
			std::filesystem::path(L"blazeclaw") / L"skills-openclaw-original",
		};
		const std::array<std::filesystem::path, 3> kWorkspaceRootSuffixes = {
			std::filesystem::path(L"skills-bundled"),
			std::filesystem::path(L"skills"),
			std::filesystem::path(L"skills-openclaw-original"),
		};

		std::filesystem::path cursor = start;
		while (!cursor.empty())
		{
			const std::wstring skillDir(
				normalizedSkillKey.begin(),
				normalizedSkillKey.end());

			for (const auto& suffix : kSkillRootSuffixes)
			{
				const auto configHtml =
					cursor /
					suffix /
					std::filesystem::path(skillDir) /
					L"config.html";
				if (std::filesystem::exists(configHtml))
				{
					return configHtml;
				}
			}

			for (const auto& suffix : kWorkspaceRootSuffixes)
			{
				const auto configHtml =
					cursor /
					suffix /
					std::filesystem::path(skillDir) /
					L"config.html";
				if (std::filesystem::exists(configHtml))
				{
					return configHtml;
				}
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

} // namespace blazeclaw::app::view_helpers
