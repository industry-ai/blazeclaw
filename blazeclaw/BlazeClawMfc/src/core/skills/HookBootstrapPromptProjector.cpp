#include "pch.h"

#include "HookBootstrapPromptProjector.h"
#include "../HookEventService.h"

namespace blazeclaw::core {

	// modify the skill prompt based on hook bootstrap files, e.g., SELF_EVOLVING_REMINDER.md
	void HookBootstrapPromptProjector::Apply(
		ProjectionContext& context) const
	{
		// detect if `SELF_EVOLVING_REMINDER.md` is present in the bootstrap files
		context.selfEvolvingHookTriggered =
			ContainsBootstrapFile(context.bootstrapFiles, L"SELF_EVOLVING_REMINDER.md");

		if (context.selfEvolvingHookTriggered &&
			context.prompt.find(L"## Self-Evolving Reminder") == std::wstring::npos)
		{
			context.prompt +=
				L"\n## Self-Evolving Reminder\n"
				L"When tasks finish, capture reusable learnings:\n"
				L"- corrections -> .learnings/LEARNINGS.md\n"
				L"- failures -> .learnings/ERRORS.md\n"
				L"- missing capabilities -> .learnings/FEATURE_REQUESTS.md\n"
				L"Promote proven patterns to AGENTS.md / SOUL.md / TOOLS.md.\n";
			context.promptChars = static_cast<std::uint32_t>(context.prompt.size());
			if (context.prompt.size() > context.maxSkillsPromptChars)
			{
				context.prompt = context.prompt.substr(0, context.maxSkillsPromptChars);
				context.promptChars =
					static_cast<std::uint32_t>(context.prompt.size());
				context.promptTruncated = true;
			}

			context.lastReminderState = L"reminder_fallback_used";
			context.lastReminderReason = L"prompt_fallback";
		}

		std::wstringstream builder;
		bool headerWritten = false;
		for (const auto& file : context.bootstrapFiles)
		{
			auto normalized = file.path;
			std::transform(
				normalized.begin(),
				normalized.end(),
				normalized.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			if (normalized == L"self_evolving_reminder.md")
			{
				continue;
			}

			if (!headerWritten)
			{
				builder << L"\n## Hook Bootstrap Context\n";
				headerWritten = true;
			}

			builder << L"- " << file.path;
			if (file.virtualFile)
			{
				builder << L" (virtual)";
			}
			builder << L"\n";
		}

		const std::wstring genericHookContext = builder.str();
		if (!genericHookContext.empty() &&
			context.prompt.find(L"## Hook Bootstrap Context") == std::wstring::npos)
		{
			context.prompt += genericHookContext;
			context.promptChars = static_cast<std::uint32_t>(context.prompt.size());
			if (context.prompt.size() > context.maxSkillsPromptChars)
			{
				context.prompt = context.prompt.substr(0, context.maxSkillsPromptChars);
				context.promptChars =
					static_cast<std::uint32_t>(context.prompt.size());
				context.promptTruncated = true;
			}
		}
	}

	bool HookBootstrapPromptProjector::ContainsBootstrapFile(
		const std::vector<HookBootstrapFile>& files,
		const std::wstring& expectedPath)
	{
		for (const auto& file : files)
		{
			auto lowered = file.path;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			auto expected = expectedPath;
			std::transform(
				expected.begin(),
				expected.end(),
				expected.begin(),
				[](const wchar_t ch)
				{
					return static_cast<wchar_t>(std::towlower(ch));
				});

			if (lowered == expected)
			{
				return true;
			}
		}

		return false;
	}

}