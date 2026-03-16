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
		const std::optional<std::string>& argsJson) {
		auto recordExecution = [&](const ToolExecuteResult& result) {
			m_executionHistory.push_back(ToolExecutionEntry{
				.tool = result.tool,
				.executed = result.executed,
				.status = result.status,
				.output = result.output,
				.argsProvided = argsJson.has_value(),
			});

			if (m_executionHistory.size() > 64) {
				m_executionHistory.erase(m_executionHistory.begin());
			}
		};

		const ToolPreviewResult preview = Preview(requestedTool);
		if (!preview.allowed) {
           const ToolExecuteResult blocked = ToolExecuteResult{
				.tool = preview.tool,
				.executed = false,
				.status = "blocked",
				.output = preview.reason,
			};
           recordExecution(blocked);
			return blocked;
		}

		if (preview.tool == "chat.send") {
			const std::string message = ExtractStringField(argsJson, "message");
			if (message.empty()) {
               const ToolExecuteResult invalid = ToolExecuteResult{
					.tool = preview.tool,
					.executed = false,
					.status = "invalid_args",
					.output = "missing_message",
				};
               recordExecution(invalid);
				return invalid;
			}

           const ToolExecuteResult sent = ToolExecuteResult{
				.tool = preview.tool,
				.executed = true,
				.status = "ok",
				.output = "sent:" + message,
			};
           recordExecution(sent);
			return sent;
		}

		if (preview.tool == "memory.search") {
			const std::string query = ExtractStringField(argsJson, "query");
           const ToolExecuteResult searched = ToolExecuteResult{
				.tool = preview.tool,
				.executed = true,
				.status = "ok",
				.output = query.empty() ? "results:seeded" : "results:seeded:" + query,
			};
           recordExecution(searched);
			return searched;
		}

       const ToolExecuteResult fallback = ToolExecuteResult{
			.tool = preview.tool,
			.executed = true,
			.status = "ok",
			.output = "seeded_execution_v1",
		};
       recordExecution(fallback);
		return fallback;
	}

	std::vector<ToolExecutionEntry> GatewayToolRegistry::ListExecutions(std::size_t limit) const {
		if (limit == 0 || m_executionHistory.empty()) {
			return {};
		}

		const std::size_t count = (std::min)(limit, m_executionHistory.size());
		std::vector<ToolExecutionEntry> output;
		output.reserve(count);

		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t index = m_executionHistory.size() - count + i;
			output.push_back(m_executionHistory[index]);
		}

		return output;
	}

	std::optional<ToolExecutionEntry> GatewayToolRegistry::LatestExecution() const {
		if (m_executionHistory.empty()) {
			return std::nullopt;
		}

		return m_executionHistory.back();
	}

	ToolExecutionStats GatewayToolRegistry::GetExecutionStats() const {
		ToolExecutionStats stats{};
		stats.count = m_executionHistory.size();

		for (const auto& execution : m_executionHistory) {
			if (execution.executed) {
				++stats.succeeded;
			}
			else {
				++stats.failed;
			}
		}

		return stats;
	}

	std::size_t GatewayToolRegistry::ClearExecutions() {
		const std::size_t cleared = m_executionHistory.size();
		m_executionHistory.clear();
		return cleared;
	}

} // namespace blazeclaw::gateway
