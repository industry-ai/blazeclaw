#pragma once

#include "IChatRunStage.h"

namespace blazeclaw::gateway {

	class ChatTransportStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

	class ChatControlStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

	class ChatDecompositionStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

	class ChatRuntimeStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

	class ChatRecoveryStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

	class ChatFinalizeStage final : public IChatRunStage {
	public:
		[[nodiscard]] const char* Name() const noexcept override;
		[[nodiscard]] ChatRunStageResult Execute(
			ChatRunStageContext& context) const override;
	};

} // namespace blazeclaw::gateway
