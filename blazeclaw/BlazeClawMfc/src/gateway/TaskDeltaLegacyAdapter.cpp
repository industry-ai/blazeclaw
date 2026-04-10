#include "pch.h"
#include "TaskDeltaLegacyAdapter.h"

namespace blazeclaw::gateway {

	GatewayHost::ChatRuntimeResult::TaskDeltaEntry TaskDeltaLegacyAdapter::AdaptEntry(
		const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
		const std::string& runId,
		const std::string& sessionId,
		const std::size_t index) {
		auto adapted = source;
		adapted.schemaVersion = 1;
		adapted.index = index;
		if (adapted.runId.empty()) {
			adapted.runId = runId;
		}
		if (adapted.sessionId.empty()) {
			adapted.sessionId = sessionId.empty() ? "main" : sessionId;
		}
		if (adapted.phase.empty()) {
			adapted.phase = index == 0 ? "plan" : "unknown";
		}
		if (adapted.status.empty()) {
			adapted.status = adapted.phase == "final" ? "completed" : "ok";
		}
		if (adapted.stepLabel.empty()) {
			adapted.stepLabel = adapted.phase == "final" ? "run_terminal" : adapted.phase;
		}
		if (adapted.startedAtMs == 0 && adapted.completedAtMs > 0) {
			adapted.startedAtMs = adapted.completedAtMs;
		}
		if (adapted.completedAtMs == 0) {
			adapted.completedAtMs = adapted.startedAtMs;
		}
		if (adapted.completedAtMs < adapted.startedAtMs) {
			adapted.completedAtMs = adapted.startedAtMs;
		}
		adapted.latencyMs = adapted.completedAtMs - adapted.startedAtMs;
		return adapted;
	}

	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> TaskDeltaLegacyAdapter::AdaptRun(
		const std::string& runId,
		const std::string& sessionId,
		const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& entries) {
		std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> adapted;
		adapted.reserve(entries.size());
		for (std::size_t i = 0; i < entries.size(); ++i) {
			adapted.push_back(AdaptEntry(entries[i], runId, sessionId, i));
		}
		return adapted;
	}

} // namespace blazeclaw::gateway
