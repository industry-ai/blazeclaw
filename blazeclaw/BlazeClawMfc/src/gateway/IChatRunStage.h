#pragma once

#include "ChatRunStageContext.h"

namespace blazeclaw::gateway {

	struct ChatRunStageResult {
		bool ok = true;
		std::string nextStage;
		std::string status;
	};

	class IChatRunStage {
	public:
		virtual ~IChatRunStage() = default;
		[[nodiscard]] virtual const char* Name() const noexcept = 0;
		[[nodiscard]] virtual ChatRunStageResult Execute(
			ChatRunStageContext& context) const = 0;
	};

} // namespace blazeclaw::gateway
