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

		GatewayToolRegistry();

		std::vector<ToolCatalogEntry> List() const;
		ToolPreviewResult Preview(const std::string& requestedTool) const;
      ToolExecuteResult Execute(
			const std::string& requestedTool,
           const std::optional<std::string>& argsJson = std::nullopt);
       void RegisterRuntimeTool(
			const ToolCatalogEntry& tool,
			RuntimeToolExecutor executor);
        std::size_t LoadExtensionToolsFromCatalog(const std::string& catalogPath);
		std::vector<ToolExecutionEntry> ListExecutions(std::size_t limit = 20) const;
		std::optional<ToolExecutionEntry> LatestExecution() const;
		ToolExecutionStats GetExecutionStats() const;
		std::size_t ClearExecutions();

	private:
		std::unordered_map<std::string, ToolCatalogEntry> m_tools;
       std::unordered_map<std::string, RuntimeToolExecutor> m_runtimeExecutors;
      std::vector<ToolExecutionEntry> m_executionHistory;
	};

} // namespace blazeclaw::gateway
