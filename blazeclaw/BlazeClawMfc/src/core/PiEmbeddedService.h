#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayToolRegistry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace blazeclaw::core {

	struct EmbeddedRunRequest {
		std::string sessionId;
		std::string agentId;
		std::string message;
	};

	struct EmbeddedRunResult {
		bool accepted = false;
		std::string runId;
		std::string status;
		std::string reason;
		std::uint64_t startedAtMs = 0;
	};

	struct EmbeddedRunRecord {
		std::string runId;
		std::string sessionId;
		std::string agentId;
		std::string message;
		std::string status;
		std::uint64_t startedAtMs = 0;
		std::optional<std::uint64_t> completedAtMs;
	};

	struct EmbeddedToolBinding {
		std::string commandName;
		std::string description;
		std::string toolName;
		std::string argMode;
	};

	struct EmbeddedRuntimeExecutionRequest {
		EmbeddedRunRequest run;
		std::string skillsPrompt;
		std::vector<EmbeddedToolBinding> toolBindings;
		std::vector<blazeclaw::gateway::ToolCatalogEntry> runtimeTools;
		bool enableDynamicToolLoop = false;
		std::function<blazeclaw::gateway::ToolExecuteResultV2(
			const blazeclaw::gateway::ToolExecuteRequestV2&)> toolExecutorV2;
		std::function<blazeclaw::gateway::ToolExecuteResult(
			const std::string&,
			const std::optional<std::string>&)> toolExecutor;
		std::function<bool()> isCancellationRequested;
	};

	struct EmbeddedTaskDelta {
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

	struct EmbeddedRuntimeExecutionResult {
		bool accepted = false;
		bool handled = false;
		bool success = false;
		std::string runId;
		std::string status;
		std::string reason;
		std::string assistantText;
		std::vector<std::string> assistantDeltas;
		std::string errorCode;
		std::string errorMessage;
		std::size_t decompositionSteps = 0;
		std::uint64_t startedAtMs = 0;
		std::vector<EmbeddedTaskDelta> taskDeltas;
	};

	class PiEmbeddedService {
	public:
		void Configure(const blazeclaw::config::AppConfig& appConfig);

		[[nodiscard]] EmbeddedRunResult QueueRun(const EmbeddedRunRequest& request);

		[[nodiscard]] EmbeddedRuntimeExecutionResult ExecuteRun(
			const EmbeddedRuntimeExecutionRequest& request);

		[[nodiscard]] bool CompleteRun(
			const std::string& runId,
			const std::string& status,
			std::uint64_t completedAtMs);

		[[nodiscard]] std::size_t ActiveRuns() const;

		[[nodiscard]] std::optional<EmbeddedRunRecord> GetRun(
			const std::string& runId) const;

		[[nodiscard]] std::vector<EmbeddedTaskDelta> GetTaskDeltas(
			const std::string& runId) const;

		void AbortRun(const std::string& runId);

		void ClearTaskDeltas(const std::string& runId);

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;

	private:
		blazeclaw::config::AppConfig m_config;
		mutable std::mutex m_cancelMutex;
		std::unordered_set<std::string> m_cancelledRunIds;
		std::unordered_map<std::string, EmbeddedRunRecord> m_runsById;
		std::unordered_map<std::string, std::vector<EmbeddedTaskDelta>>
			m_taskDeltasByRunId;

		void AppendTaskDelta(
			const std::string& runId,
			const EmbeddedTaskDelta& delta);

		[[nodiscard]] bool IsRunCancelled(const std::string& runId) const;
		void ClearRunCancellation(const std::string& runId);
	};

} // namespace blazeclaw::core
