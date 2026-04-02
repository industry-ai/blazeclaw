#pragma once

#include "../app/pch.h"

namespace blazeclaw::gateway {

	struct ToolCatalogEntry {
		std::string id;
		std::string label;
		std::string category;
		bool enabled = true;
	};

	struct ToolPreviewResult {
		std::string tool;
		bool allowed = false;
		std::string reason;
	};

	struct ToolExecuteResult {
		std::string tool;
		bool executed = false;
		std::string status;
		std::string output;
	};

	struct ToolExecuteRequestV2 {
		std::string tool;
		std::optional<std::string> argsJson;
		std::string correlationId;
		std::optional<std::uint64_t> deadlineEpochMs;
	};

	struct ToolExecuteResultV2 {
		std::string tool;
		bool executed = false;
		std::string status;
		std::string result;
		std::string errorCode;
		std::string errorMessage;
		std::uint64_t startedAtMs = 0;
		std::uint64_t completedAtMs = 0;
		std::uint64_t latencyMs = 0;
		std::string correlationId;
	};

	struct ToolExecutionEntry {
		std::string tool;
		bool executed = false;
		std::string status;
		std::string output;
		bool argsProvided = false;
	};

	struct ToolExecutionStats {
		std::size_t count = 0;
		std::size_t succeeded = 0;
		std::size_t failed = 0;
	};

	class GatewayToolRegistry {
	public:
		using RuntimeToolExecutor = std::function<ToolExecuteResult(
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson)>;
		using RuntimeToolExecutorV2 = std::function<ToolExecuteResultV2(
			const ToolExecuteRequestV2& request)>;

		GatewayToolRegistry();

		// Allow external lifecycle manager to register tools in bulk
		friend class ExtensionLifecycleManager;

		std::vector<ToolCatalogEntry> List() const;
		ToolPreviewResult Preview(const std::string& requestedTool) const;
		ToolExecuteResult Execute(
			const std::string& requestedTool,
			const std::optional<std::string>& argsJson = std::nullopt);
		ToolExecuteResultV2 ExecuteV2(const ToolExecuteRequestV2& request);
		void RegisterRuntimeTool(
			const ToolCatalogEntry& tool,
			RuntimeToolExecutor executor);
		void RegisterRuntimeToolV2(
			const ToolCatalogEntry& tool,
			RuntimeToolExecutorV2 executor);

		void UnregisterRuntimeTool(const std::string& toolId);
		std::size_t LoadExtensionToolsFromCatalog(const std::string& catalogPath);
		std::vector<ToolExecutionEntry> ListExecutions(std::size_t limit = 20) const;
		std::optional<ToolExecutionEntry> LatestExecution() const;
		ToolExecutionStats GetExecutionStats() const;
		std::size_t ClearExecutions();

	private:
		std::unordered_map<std::string, ToolCatalogEntry> m_tools;
		std::unordered_map<std::string, RuntimeToolExecutor> m_runtimeExecutors;
		std::unordered_map<std::string, RuntimeToolExecutorV2> m_runtimeExecutorsV2;
		std::vector<ToolExecutionEntry> m_executionHistory;
	};

} // namespace blazeclaw::gateway
