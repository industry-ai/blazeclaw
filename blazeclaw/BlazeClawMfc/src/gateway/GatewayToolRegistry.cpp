#include "pch.h"
#include "GatewayToolRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
		std::string ExtractStringField(
			const std::optional<std::string>& json,
			const std::string& fieldName) {
			if (!json.has_value()) {
				return {};
			}

			const std::string& text = json.value();
			const std::string token = "\"" + fieldName + "\"";
			const std::size_t keyPos = text.find(token);
			if (keyPos == std::string::npos) {
				return {};
			}

			std::size_t valuePos = text.find(':', keyPos + token.size());
			if (valuePos == std::string::npos) {
				return {};
			}

			valuePos = text.find('"', valuePos);
			if (valuePos == std::string::npos) {
				return {};
			}

			const std::size_t endQuote = text.find('"', valuePos + 1);
			if (endQuote == std::string::npos) {
				return {};
			}

			return text.substr(valuePos + 1, endQuote - valuePos - 1);
		}
	}

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

    ToolExecuteResult GatewayToolRegistry::Execute(
		const std::string& requestedTool,
		const std::optional<std::string>& argsJson) const {
		const ToolPreviewResult preview = Preview(requestedTool);
		if (!preview.allowed) {
			return ToolExecuteResult{
				.tool = preview.tool,
				.executed = false,
				.status = "blocked",
				.output = preview.reason,
			};
		}

		if (preview.tool == "chat.send") {
			const std::string message = ExtractStringField(argsJson, "message");
			if (message.empty()) {
				return ToolExecuteResult{
					.tool = preview.tool,
					.executed = false,
					.status = "invalid_args",
					.output = "missing_message",
				};
			}

			return ToolExecuteResult{
				.tool = preview.tool,
				.executed = true,
				.status = "ok",
				.output = "sent:" + message,
			};
		}

		if (preview.tool == "memory.search") {
			const std::string query = ExtractStringField(argsJson, "query");
			return ToolExecuteResult{
				.tool = preview.tool,
				.executed = true,
				.status = "ok",
				.output = query.empty() ? "results:seeded" : "results:seeded:" + query,
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
