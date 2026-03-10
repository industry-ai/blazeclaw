#include "pch.h"
#include "GatewayToolRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

	GatewayToolRegistry::GatewayToolRegistry() {
		m_tools.insert_or_assign(
			"chat.send",
			ToolCatalogEntry{
				.id = "chat.send",
				.label = "Chat Send",
				.category = "messaging",
				.enabled = true,
			});
		m_tools.insert_or_assign(
			"memory.search",
			ToolCatalogEntry{
				.id = "memory.search",
				.label = "Memory Search",
				.category = "knowledge",
				.enabled = true,
			});
	}

	std::vector<ToolCatalogEntry> GatewayToolRegistry::List() const {
		std::vector<ToolCatalogEntry> output;
		output.reserve(m_tools.size());

		for (const auto& [_, tool] : m_tools) {
			output.push_back(tool);
		}

		std::sort(output.begin(), output.end(), [](const ToolCatalogEntry& left, const ToolCatalogEntry& right) {
			return left.id < right.id;
			});

		return output;
	}

	ToolPreviewResult GatewayToolRegistry::Preview(const std::string& requestedTool) const {
		const std::string tool = requestedTool.empty() ? "" : requestedTool;
		if (tool.empty()) {
			return ToolPreviewResult{
				.tool = "none",
				.allowed = false,
				.reason = "missing_tool",
			};
		}

		const auto it = m_tools.find(tool);
		if (it == m_tools.end()) {
			return ToolPreviewResult{
				.tool = requestedTool,
				.allowed = false,
				.reason = "unknown_tool",
			};
		}

		return ToolPreviewResult{
			.tool = it->second.id,
			.allowed = it->second.enabled,
			.reason = it->second.enabled ? "ready" : "disabled",
		};
	}

	ToolExecuteResult GatewayToolRegistry::Execute(const std::string& requestedTool) const {
		const ToolPreviewResult preview = Preview(requestedTool);
		if (!preview.allowed) {
			return ToolExecuteResult{
				.tool = preview.tool,
				.executed = false,
				.status = "blocked",
				.output = preview.reason,
			};
		}

		return ToolExecuteResult{
			.tool = preview.tool,
			.executed = true,
			.status = "ok",
			.output = "seeded_execution_v1",
		};
	}

} // namespace blazeclaw::gateway
