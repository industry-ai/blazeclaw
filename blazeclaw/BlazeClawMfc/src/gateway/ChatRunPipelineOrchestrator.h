#pragma once

#include "ChatRunStages.h"

namespace blazeclaw::gateway {

	class ChatRunPipelineOrchestrator {
	public:
		ChatRunPipelineOrchestrator();

		[[nodiscard]] ChatRunStageResult Run(ChatRunStageContext& context) const;

	private:
		ChatTransportStage m_transportStage;
		ChatControlStage m_controlStage;
		ChatDecompositionStage m_decompositionStage;
		ChatRuntimeStage m_runtimeStage;
		ChatRecoveryStage m_recoveryStage;
		ChatFinalizeStage m_finalizeStage;
	};

} // namespace blazeclaw::gateway
