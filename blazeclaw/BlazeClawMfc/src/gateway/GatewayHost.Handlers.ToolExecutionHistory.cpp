#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersToolExecution.h"
#include "GatewayJsonSerializers.h"
#include "GatewayToolRegistry.h"

namespace blazeclaw::gateway::handlers::tool_execution {

protocol::ResponseFrame ToolExecutionHistoryHandlers::HandleExecutionsList(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const auto executions = registry.ListExecutions(20);
	std::string executionsJson = "[";
	for (std::size_t i = 0; i < executions.size(); ++i) {
		if (i > 0) {
			executionsJson += ",";
		}
		executionsJson += SerializeToolExecution(executions[i]);
	}
	executionsJson += "]";

	return protocol::OkResponse(
		request,
		"{\"executions\":" + executionsJson + ",\"count\":" + std::to_string(executions.size()) + "}");
}

protocol::ResponseFrame ToolExecutionHistoryHandlers::HandleExecutionsCount(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const ToolExecutionStats stats = registry.GetExecutionStats();
	return protocol::OkResponse(
		request,
		"{\"count\":" + std::to_string(stats.count) +
			",\"succeeded\":" + std::to_string(stats.succeeded) +
			",\"failed\":" + std::to_string(stats.failed) + "}");
}

protocol::ResponseFrame ToolExecutionHistoryHandlers::HandleExecutionsLatest(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const std::optional<ToolExecutionEntry> latest = registry.LatestExecution();
	const ToolExecutionEntry fallback = ToolExecutionEntry{
		.tool = "none",
		.executed = false,
		.status = "empty",
		.output = "no_history",
		.argsProvided = false,
	};

	const ToolExecutionEntry& selected = latest.has_value() ? latest.value() : fallback;
	const ToolExecutionStats stats = registry.GetExecutionStats();

	return protocol::OkResponse(
		request,
		"{\"found\":" + std::string(latest.has_value() ? "true" : "false") +
			",\"execution\":" + SerializeToolExecution(selected) +
			",\"count\":" + std::to_string(stats.count) + "}");
}

protocol::ResponseFrame ToolExecutionHistoryHandlers::HandleExecutionsClear(
	const protocol::RequestFrame& request,
	GatewayToolRegistry& registry) {
	const std::size_t cleared = registry.ClearExecutions();
	return protocol::OkResponse(
		request,
		"{\"cleared\":" + std::to_string(cleared) + ",\"remaining\":0}");
}

} // namespace blazeclaw::gateway::handlers::tool_execution

namespace blazeclaw::gateway {

void GatewayHost::RegisterToolExecutionHistoryHandlers() {
	using handlers::tool_execution::ToolExecutionHistoryHandlers;
	m_dispatcher.Register("gateway.tools.executions.list", [this](const protocol::RequestFrame& request) {
		return ToolExecutionHistoryHandlers::HandleExecutionsList(request, m_toolRegistry);
		});

	m_dispatcher.Register("gateway.tools.executions.count", [this](const protocol::RequestFrame& request) {
		return ToolExecutionHistoryHandlers::HandleExecutionsCount(request, m_toolRegistry);
		});

	m_dispatcher.Register("gateway.tools.executions.latest", [this](const protocol::RequestFrame& request) {
		return ToolExecutionHistoryHandlers::HandleExecutionsLatest(request, m_toolRegistry);
		});

	m_dispatcher.Register("gateway.tools.executions.clear", [this](const protocol::RequestFrame& request) {
		return ToolExecutionHistoryHandlers::HandleExecutionsClear(request, m_toolRegistry);
		});
}

} // namespace blazeclaw::gateway
