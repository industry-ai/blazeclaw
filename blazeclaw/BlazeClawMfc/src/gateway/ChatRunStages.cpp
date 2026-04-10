#include "pch.h"
#include "ChatRunStages.h"

namespace blazeclaw::gateway {

	namespace {

		ChatRunStageResult AppendStage(
			ChatRunStageContext& context,
			const char* stageName,
			std::string nextStage,
			std::string status) {
			context.stageTrace.push_back(stageName == nullptr ? std::string{} : std::string(stageName));
			return ChatRunStageResult{
				.ok = true,
				.nextStage = std::move(nextStage),
				.status = std::move(status),
			};
		}

	} // namespace

	const char* ChatTransportStage::Name() const noexcept {
		return "transport";
	}

	ChatRunStageResult ChatTransportStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "control", "ok");
	}

	const char* ChatControlStage::Name() const noexcept {
		return "control";
	}

	ChatRunStageResult ChatControlStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "decomposition", "ok");
	}

	const char* ChatDecompositionStage::Name() const noexcept {
		return "decomposition";
	}

	ChatRunStageResult ChatDecompositionStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "runtime", "ok");
	}

	const char* ChatRuntimeStage::Name() const noexcept {
		return "runtime";
	}

	ChatRunStageResult ChatRuntimeStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "recovery", "ok");
	}

	const char* ChatRecoveryStage::Name() const noexcept {
		return "recovery";
	}

	ChatRunStageResult ChatRecoveryStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), "finalize", "ok");
	}

	const char* ChatFinalizeStage::Name() const noexcept {
		return "finalize";
	}

	ChatRunStageResult ChatFinalizeStage::Execute(ChatRunStageContext& context) const {
		return AppendStage(context, Name(), {}, "completed");
	}

} // namespace blazeclaw::gateway
