#include "pch.h"
#include "ChatRunPipelineOrchestrator.h"

namespace blazeclaw::gateway {

	ChatRunPipelineOrchestrator::ChatRunPipelineOrchestrator() = default;

	ChatRunStageResult ChatRunPipelineOrchestrator::Run(
		ChatRunStageContext& context) const {
		auto result = m_transportStage.Execute(context);
		if (!result.ok) {
			return result;
		}

		result = m_controlStage.Execute(context);
		if (!result.ok) {
			return result;
		}

		result = m_decompositionStage.Execute(context);
		if (!result.ok) {
			return result;
		}

		result = m_runtimeStage.Execute(context);
		if (!result.ok) {
			return result;
		}

		result = m_recoveryStage.Execute(context);
		if (!result.ok) {
			return result;
		}

		return m_finalizeStage.Execute(context);
	}

} // namespace blazeclaw::gateway
