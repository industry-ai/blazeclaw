#include "pch.h"
#include "GatewayToolRegistry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace blazeclaw::gateway {
	namespace {
		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		ToolExecuteResultV2 AdaptLegacyResultToV2(
			const ToolExecuteResult& legacy,
			const ToolExecuteRequestV2& request,
			const std::uint64_t startedAtMs,
			const std::uint64_t completedAtMs) {
			ToolExecuteResultV2 v2;
			v2.tool = legacy.tool;
			v2.executed = legacy.executed;
			v2.status = legacy.status;
			v2.result = legacy.output;
			v2.errorCode = (legacy.executed && legacy.status != "error")
				? std::string()
				: std::string("legacy_execution_failed");
			v2.errorMessage = (legacy.executed && legacy.status != "error")
				? std::string()
				: legacy.output;
			v2.startedAtMs = startedAtMs;
			v2.completedAtMs = completedAtMs;
			v2.latencyMs = completedAtMs >= startedAtMs
				? (completedAtMs - startedAtMs)
				: 0;
			v2.correlationId = request.correlationId;
			return v2;
		}

		void NormalizeBaiduFailureStatus(ToolExecuteResultV2& result) {
			if (result.tool != "baidu-search.search.web") {
				return;
			}

			if (result.status == "process_exit_nonzero") {
				result.status = "script_runtime_error";
			}

			if (result.errorCode == "process_exit_nonzero") {
				result.errorCode = "script_runtime_error";
			}

			if (result.errorMessage.empty() && !result.result.empty()) {
				result.errorMessage = result.result;
			}
		}

		std::string ExtractSkillKeyFromToolId(const std::string& toolId) {
			const auto dot = toolId.find('.');
			if (dot == std::string::npos || dot == 0) {
				return toolId;
			}

			return toolId.substr(0, dot);
		}

		std::vector<std::string> SplitTopLevelObjects(const std::string& arrayJson) {
			std::vector<std::string> objects;
			const std::string trimmed = json::Trim(arrayJson);
			if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
				return objects;
			}

			std::size_t index = 1;
			while (index + 1 < trimmed.size()) {
				while (index + 1 < trimmed.size() &&
					(std::isspace(static_cast<unsigned char>(trimmed[index])) != 0 ||
						trimmed[index] == ',')) {
					++index;
				}

				if (index + 1 >= trimmed.size() || trimmed[index] != '{') {
					break;
				}

				const std::size_t begin = index;
				int depth = 0;
				bool inString = false;
				for (; index < trimmed.size(); ++index) {
					const char ch = trimmed[index];
					if (inString) {
						if (ch == '\\') {
							++index;
							continue;
						}

						if (ch == '"') {
							inString = false;
						}
						continue;
					}

					if (ch == '"') {
						inString = true;
						continue;
					}

					if (ch == '{') {
						++depth;
					}
					else if (ch == '}') {
						--depth;
						if (depth == 0) {
							objects.push_back(trimmed.substr(begin, index - begin + 1));
							++index;
							break;
						}
					}
				}
			}

			return objects;
		}

		std::string DirectoryName(const std::string& path) {
			if (path.empty()) {
				return {};
			}

			const std::size_t pos = path.find_last_of("/\\");
			if (pos == std::string::npos) {
				return {};
			}

			return path.substr(0, pos);
		}

		std::string JoinPath(const std::string& left, const std::string& right) {
			if (left.empty()) {
				return right;
			}

			if (right.empty()) {
				return left;
			}

			const bool leftEndsWithSeparator =
				left.back() == '/' || left.back() == '\\';
			if (leftEndsWithSeparator) {
				return left + right;
			}

			return left + "/" + right;
		}

		std::string ReadFileUtf8(const std::string& filePath) {
			std::ifstream input(filePath, std::ios::binary);
			if (!input.is_open()) {
				return {};
			}

			std::string text;
			input.seekg(0, std::ios::end);
			const auto size = input.tellg();
			if (size > 0) {
				text.resize(static_cast<std::size_t>(size));
				input.seekg(0, std::ios::beg);
				input.read(text.data(), static_cast<std::streamsize>(text.size()));
			}

			return text;
		}

		bool ExtractBoolField(
			const std::string& jsonText,
			const std::string& fieldName,
			const bool fallback) {
			bool value = fallback;
			if (json::FindBoolField(jsonText, fieldName, value)) {
				return value;
			}

			return fallback;
		}
	}

	GatewayToolRegistry::GatewayToolRegistry() = default;

	std::vector<ToolCatalogEntry> GatewayToolRegistry::List() const {
		std::vector<ToolCatalogEntry> output;
		output.reserve(m_tools.size());

		for (const auto& [_, tool] : m_tools) {
			ToolCatalogEntry normalized = tool;
			if (normalized.skillKey.empty()) {
				normalized.skillKey = ExtractSkillKeyFromToolId(normalized.id);
			}
			if (normalized.installKind.empty()) {
				normalized.installKind = normalized.category;
			}
			if (normalized.source.empty()) {
				normalized.source = "runtime.tool.registry";
			}
			output.push_back(std::move(normalized));
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

		const auto runtimeIt = m_runtimeExecutors.find(preview.tool);
		if (runtimeIt != m_runtimeExecutors.end() && runtimeIt->second) {
			const ToolExecuteResult runtimeResult = runtimeIt->second(preview.tool, argsJson);
			recordExecution(runtimeResult);
			return runtimeResult;
		}

		const ToolExecuteResult unavailableRuntime = ToolExecuteResult{
			.tool = preview.tool,
			.executed = false,
			.status = "unavailable_runtime",
			.output = "runtime_executor_missing",
		};
		recordExecution(unavailableRuntime);
		return unavailableRuntime;
	}

	ToolExecuteResultV2 GatewayToolRegistry::ExecuteV2(
		const ToolExecuteRequestV2& request) {
		const std::uint64_t startedAtMs = CurrentEpochMs();
		const auto v2It = m_runtimeExecutorsV2.find(request.tool);
		if (v2It != m_runtimeExecutorsV2.end() && v2It->second) {
			ToolExecuteResultV2 result = v2It->second(request);
			if (result.tool.empty()) {
				result.tool = request.tool;
			}
			if (result.correlationId.empty()) {
				result.correlationId = request.correlationId;
			}
			if (result.startedAtMs == 0) {
				result.startedAtMs = startedAtMs;
			}
			if (result.completedAtMs == 0) {
				result.completedAtMs = CurrentEpochMs();
			}
			result.latencyMs = result.completedAtMs >= result.startedAtMs
				? (result.completedAtMs - result.startedAtMs)
				: 0;
			NormalizeBaiduFailureStatus(result);

			m_executionHistory.push_back(ToolExecutionEntry{
				.tool = result.tool,
				.executed = result.executed,
				.status = result.status,
				.output = result.result,
				.argsProvided = request.argsJson.has_value(),
				});
			if (m_executionHistory.size() > 64) {
				m_executionHistory.erase(m_executionHistory.begin());
			}
			return result;
		}

		const ToolExecuteResult legacy = Execute(request.tool, request.argsJson);
		const std::uint64_t completedAtMs = CurrentEpochMs();
       ToolExecuteResultV2 adapted = AdaptLegacyResultToV2(
			legacy,
			request,
			startedAtMs,
			completedAtMs);
       NormalizeBaiduFailureStatus(adapted);
		return adapted;
	}

	void GatewayToolRegistry::RegisterRuntimeTool(
		const ToolCatalogEntry& tool,
		RuntimeToolExecutor executor) {
		if (tool.id.empty()) {
			return;
		}

		ToolCatalogEntry normalized = tool;
		if (normalized.label.empty()) {
			normalized.label = tool.id;
		}
		if (normalized.category.empty()) {
			normalized.category = "extension";
		}

		m_tools.insert_or_assign(normalized.id, std::move(normalized));
		if (executor) {
			m_runtimeExecutors.insert_or_assign(tool.id, std::move(executor));
		}
	}

	void GatewayToolRegistry::RegisterRuntimeToolV2(
		const ToolCatalogEntry& tool,
		RuntimeToolExecutorV2 executor) {
		if (tool.id.empty()) {
			return;
		}

		ToolCatalogEntry normalized = tool;
		if (normalized.label.empty()) {
			normalized.label = tool.id;
		}
		if (normalized.category.empty()) {
			normalized.category = "extension";
		}

		m_tools.insert_or_assign(normalized.id, std::move(normalized));
		if (executor) {
			m_runtimeExecutorsV2.insert_or_assign(tool.id, std::move(executor));
		}
	}

	void GatewayToolRegistry::UnregisterRuntimeTool(const std::string& toolId) {
		if (toolId.empty()) {
			return;
		}

		m_runtimeExecutors.erase(toolId);
		m_runtimeExecutorsV2.erase(toolId);
		m_tools.erase(toolId);
	}

	std::size_t GatewayToolRegistry::LoadExtensionToolsFromCatalog(
		const std::string& catalogPath) {
		const std::string resolvedCatalog = catalogPath;
		const std::string catalogText = ReadFileUtf8(resolvedCatalog);
		if (catalogText.empty()) {
			return 0;
		}

		const std::string catalogDirectory = DirectoryName(resolvedCatalog);

		std::string extensionsRaw;
		if (!json::FindRawField(catalogText, "extensions", extensionsRaw)) {
			return 0;
		}

		std::size_t registered = 0;
		const auto extensionEntries = SplitTopLevelObjects(extensionsRaw);
		for (const auto& entryJson : extensionEntries) {
			if (!ExtractBoolField(entryJson, "enabled", true)) {
				continue;
			}

			std::string extensionPathValue;
			if (!json::FindStringField(entryJson, "path", extensionPathValue) ||
				extensionPathValue.empty()) {
				continue;
			}

			const std::string manifestPath =
				JoinPath(catalogDirectory, extensionPathValue);
			const std::string manifestText = ReadFileUtf8(manifestPath);
			if (manifestText.empty()) {
				continue;
			}

			std::string toolsRaw;
			if (!json::FindRawField(manifestText, "tools", toolsRaw)) {
				continue;
			}

			for (const auto& toolJson : SplitTopLevelObjects(toolsRaw)) {
				std::string toolId;
				if (!json::FindStringField(toolJson, "id", toolId) || toolId.empty()) {
					continue;
				}

				std::string label;
				json::FindStringField(toolJson, "label", label);
				std::string category;
				json::FindStringField(toolJson, "category", category);
				const bool enabled = ExtractBoolField(toolJson, "enabled", true);

				m_tools.insert_or_assign(
					toolId,
					ToolCatalogEntry{
						.id = toolId,
						.label = label.empty() ? toolId : label,
						.category = category.empty() ? "extension" : category,
					 .skillKey = ExtractSkillKeyFromToolId(toolId),
						.installKind = "runtime-registered",
						.source = "extensions.catalog",
						.enabled = enabled,
					});
				++registered;
			}
		}

		return registered;
	}

	std::size_t GatewayToolRegistry::LoadSkillToolsFromDirectory(
		const std::string& skillsDirectory) {
		if (skillsDirectory.empty()) {
			return 0;
		}

		std::error_code ec;
		const std::filesystem::path skillsRoot(skillsDirectory);
		if (!std::filesystem::exists(skillsRoot, ec) ||
			!std::filesystem::is_directory(skillsRoot, ec)) {
			return 0;
		}

		std::size_t registered = 0;
		for (const auto& entry : std::filesystem::directory_iterator(skillsRoot, ec)) {
			if (ec || !entry.is_directory()) {
				continue;
			}

			const std::filesystem::path manifestPath =
				entry.path() / "tool-manifest.json";
			if (!std::filesystem::exists(manifestPath, ec) ||
				!std::filesystem::is_regular_file(manifestPath, ec)) {
				continue;
			}

			const std::string manifestText = ReadFileUtf8(manifestPath.string());
			if (manifestText.empty()) {
				continue;
			}

			std::string toolsRaw;
			if (!json::FindRawField(manifestText, "tools", toolsRaw)) {
				continue;
			}

			for (const auto& toolJson : SplitTopLevelObjects(toolsRaw)) {
				std::string toolId;
				if (!json::FindStringField(toolJson, "id", toolId) || toolId.empty()) {
					continue;
				}

				std::string label;
				json::FindStringField(toolJson, "label", label);
				std::string category;
				json::FindStringField(toolJson, "category", category);
				const bool enabled = ExtractBoolField(toolJson, "enabled", true);

				m_tools.insert_or_assign(
					toolId,
					ToolCatalogEntry{
						.id = toolId,
						.label = label.empty() ? toolId : label,
						.category = category.empty() ? "skill" : category,
					 .skillKey = ExtractSkillKeyFromToolId(toolId),
						.installKind = "skill",
						.source = "skills.tool-manifest",
						.enabled = enabled,
					});
				++registered;
			}
		}

		return registered;
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
