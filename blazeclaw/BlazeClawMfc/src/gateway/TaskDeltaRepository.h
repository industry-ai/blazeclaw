#pragma once

#include "GatewayProtocolModels.h"

#include <unordered_map>
#include <vector>
#include <optional>

namespace blazeclaw::gateway {

	struct TaskDeltaEntry {
		std::uint32_t schemaVersion = 1;
		std::size_t index = 0;
		std::string runId;
		std::string sessionId;
		std::string phase;
		std::string toolName;
		std::string fallbackBackend;
		std::string fallbackAction;
		std::size_t fallbackAttempt = 0;
		std::size_t fallbackMaxAttempts = 0;
		std::string argsJson;
		std::string resultJson;
		std::string status;
		std::string errorCode;
		std::uint64_t startedAtMs = 0;
		std::uint64_t completedAtMs = 0;
		std::uint64_t latencyMs = 0;
		std::string modelTurnId;
		std::string stepLabel;
	};

	class TaskDeltaRepository {
	public:
		using Store = std::unordered_map<std::string, std::vector<TaskDeltaEntry>>;

		explicit TaskDeltaRepository(Store& backingStore);

		[[nodiscard]] bool Upsert(
			const std::string& runId,
			const std::vector<TaskDeltaEntry>& entries);

		[[nodiscard]] std::optional<std::vector<TaskDeltaEntry>> Get(
			const std::string& runId) const;

		[[nodiscard]] bool Clear(const std::string& runId);
		void ClearAll();

		[[nodiscard]] std::size_t Size() const noexcept;
		[[nodiscard]] const Store& Snapshot() const noexcept;

	private:
		Store& m_backingStore;
	};

} // namespace blazeclaw::gateway
