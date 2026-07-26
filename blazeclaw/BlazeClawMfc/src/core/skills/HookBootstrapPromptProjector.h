// HookBootstrapPromptProjector.h
#pragma once

#include "../HookCatalogService.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct HookBootstrapFile;
	class HookBootstrapPromptProjector {
	public:
		struct ProjectionContext {
			const std::vector<HookBootstrapFile>& bootstrapFiles;
			std::wstring& prompt;
			std::uint32_t& promptChars;
			bool& promptTruncated;
			std::size_t maxSkillsPromptChars = 0;
			std::wstring& lastReminderState;
			std::wstring& lastReminderReason;
			bool& selfEvolvingHookTriggered;
		};

		void Apply(ProjectionContext& context) const;

		[[nodiscard]] static bool ContainsBootstrapFile(
			const std::vector<HookBootstrapFile>& files,
			const std::wstring& expectedPath);
	};

}