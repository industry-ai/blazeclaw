#include "pch.h"
#include "TaskDeltaSchemaValidator.h"

namespace blazeclaw::gateway {

	bool TaskDeltaSchemaValidator::ValidateEntry(
		const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& entry,
		std::string& errorCode,
		std::string& errorMessage) {
		errorCode.clear();
		errorMessage.clear();

		if (entry.schemaVersion != 1) {
			errorCode = "unsupported_schema_version";
			errorMessage = "Task delta entry schemaVersion must be 1.";
			return false;
		}

		if (entry.runId.empty()) {
			errorCode = "missing_run_id";
			errorMessage = "Task delta entry requires runId.";
			return false;
		}

		if (entry.phase.empty()) {
			errorCode = "missing_phase";
			errorMessage = "Task delta entry requires phase.";
			return false;
		}

		if (entry.status.empty()) {
			errorCode = "missing_status";
			errorMessage = "Task delta entry requires status.";
			return false;
		}

		if (entry.stepLabel.empty()) {
			errorCode = "missing_step_label";
			errorMessage = "Task delta entry requires stepLabel.";
			return false;
		}

		if (entry.completedAtMs < entry.startedAtMs) {
			errorCode = "invalid_timestamp_order";
			errorMessage = "Task delta completedAtMs must be >= startedAtMs.";
			return false;
		}

		if (entry.phase == "final" && entry.status.empty()) {
			errorCode = "invalid_final_status";
			errorMessage = "Final phase task delta requires terminal status.";
			return false;
		}

		return true;
	}

	bool TaskDeltaSchemaValidator::ValidateRun(
		const std::string& runId,
		const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& entries,
		std::string& errorCode,
		std::string& errorMessage) {
		errorCode.clear();
		errorMessage.clear();

		if (runId.empty()) {
			errorCode = "missing_run_id";
			errorMessage = "Task delta run requires runId.";
			return false;
		}

		if (entries.empty()) {
			return true;
		}

		std::size_t expectedIndex = 0;
		for (const auto& entry : entries) {
			if (entry.runId != runId) {
				errorCode = "run_id_mismatch";
				errorMessage = "Task delta entry runId must match run bucket id.";
				return false;
			}

			if (entry.index != expectedIndex) {
				errorCode = "non_deterministic_index";
				errorMessage = "Task delta entries must use deterministic contiguous indexes.";
				return false;
			}

			if (!ValidateEntry(entry, errorCode, errorMessage)) {
				return false;
			}

			++expectedIndex;
		}

		return true;
	}

} // namespace blazeclaw::gateway
