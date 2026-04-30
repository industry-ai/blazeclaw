#include "pch.h"
#include "GatewayToolRegistry.h"
#include "GatewayJsonUtils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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

		bool IsSkillToolSource(const std::string& source) {
			return source == "skills.catalog" || source == "skills.tool-manifest";
		}

		std::string JsonEscape(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}
			return escaped;
		}

		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool IsTransientSkillDirectoryName(const std::string& dirName) {
			const std::string lowerName = ToLowerCopy(dirName);
			return (lowerName.size() >= 4 &&
				lowerName.compare(lowerName.size() - 4, 4, ".tmp") == 0) ||
				(lowerName.size() >= 5 &&
					lowerName.compare(lowerName.size() - 5, 5, ".temp") == 0);
		}

		std::string NormalizeSkillDirectoryPath(const std::filesystem::path& path) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(path, ec);
			if (!ec) {
				return canonical.lexically_normal().string();
			}

			const auto absolute = std::filesystem::absolute(path, ec);
			if (!ec) {
				return absolute.lexically_normal().string();
			}

			return path.lexically_normal().string();
		}

		std::uint64_t HashCombine(const std::uint64_t seed, const std::uint64_t value) {
			return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
		}

		std::uint64_t BuildSkillDirectoryManifestFingerprint(
			const std::filesystem::path& skillsRoot) {
			std::error_code ec;
			std::vector<std::string> manifestPaths;
			for (const auto& entry : std::filesystem::directory_iterator(skillsRoot, ec)) {
				if (ec || !entry.is_directory()) {
					continue;
				}

				const std::string dirName = entry.path().filename().string();
				if (IsTransientSkillDirectoryName(dirName)) {
					continue;
				}

				const std::filesystem::path manifestPath = entry.path() / "tool-manifest.json";
				if (!std::filesystem::exists(manifestPath, ec) ||
					!std::filesystem::is_regular_file(manifestPath, ec)) {
					continue;
				}

				manifestPaths.push_back(NormalizeSkillDirectoryPath(manifestPath));
			}

			std::sort(manifestPaths.begin(), manifestPaths.end());
			std::uint64_t fingerprint = static_cast<std::uint64_t>(manifestPaths.size());
			for (const auto& manifestPathText : manifestPaths) {
				const std::filesystem::path manifestPath(manifestPathText);
				ec.clear();
				const auto writeTime = std::filesystem::last_write_time(manifestPath, ec);
				std::uint64_t writeHash = 0;
				if (!ec) {
					const auto ticks = writeTime.time_since_epoch().count();
					writeHash = static_cast<std::uint64_t>(
						std::hash<long long>{}(static_cast<long long>(ticks)));
				}
				fingerprint = HashCombine(
					fingerprint,
					static_cast<std::uint64_t>(std::hash<std::string>{}(manifestPathText)));
				fingerprint = HashCombine(fingerprint, writeHash);
			}

			return fingerprint;
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

	std::size_t GatewayToolRegistry::RegisterSkillToolsFromCatalogEntries(
		const std::vector<ToolCatalogEntry>& skillTools,
		const bool resetCatalogSource) {
		if (resetCatalogSource) {
			for (const auto& [toolId, _] : m_catalogSkillTools) {
				const auto it = m_tools.find(toolId);
				if (it != m_tools.end() && it->second.source == "skills.catalog") {
					m_tools.erase(it);
				}
			}
			m_catalogSkillTools.clear();
			m_skillDirectoryLoadCache.clear();
		}

		std::size_t registered = 0;
		for (const auto& incoming : skillTools) {
			if (incoming.id.empty()) {
				++m_skillToolSourceDiagnostics.catalogRejected;
				continue;
			}

			ToolCatalogEntry normalized = incoming;
			if (normalized.label.empty()) {
				normalized.label = normalized.id;
			}
			if (normalized.category.empty()) {
				normalized.category = "skill";
			}
			if (normalized.skillKey.empty()) {
				normalized.skillKey = ExtractSkillKeyFromToolId(normalized.id);
			}
			if (normalized.installKind.empty()) {
				normalized.installKind = "skill";
			}
			normalized.source = "skills.catalog";

			m_catalogSkillTools.insert_or_assign(normalized.id, normalized);
			m_tools.insert_or_assign(normalized.id, std::move(normalized));
			++registered;
		}

		m_skillToolSourceDiagnostics.catalogRegistered += registered;
		return registered;
	}

	std::size_t GatewayToolRegistry::SyncSkillToolsManifestFirst(
		const std::vector<std::string>& skillDirectories,
		const std::vector<ToolCatalogEntry>& catalogSkillTools,
		const bool resetExistingSkillSources) {
		if (resetExistingSkillSources) {
			for (auto it = m_tools.begin(); it != m_tools.end();) {
				if (IsSkillToolSource(it->second.source)) {
					it = m_tools.erase(it);
				}
				else {
					++it;
				}
			}
			m_catalogSkillTools.clear();
			m_skillDirectoryLoadCache.clear();
		}

		std::unordered_map<std::string, std::vector<ToolCatalogEntry>> toolsBySkill;
		for (const auto& incoming : catalogSkillTools) {
			if (incoming.id.empty()) {
				++m_skillToolSourceDiagnostics.catalogRejected;
				continue;
			}

			ToolCatalogEntry normalized = incoming;
			if (normalized.label.empty()) {
				normalized.label = normalized.id;
			}
			if (normalized.category.empty()) {
				normalized.category = "skill";
			}
			if (normalized.skillKey.empty()) {
				normalized.skillKey = ExtractSkillKeyFromToolId(normalized.id);
			}
			if (normalized.installKind.empty()) {
				normalized.installKind = "skill";
			}
			normalized.source = "skills.catalog";
			m_catalogSkillTools.insert_or_assign(normalized.id, normalized);
			toolsBySkill[ToLowerCopy(normalized.skillKey)].push_back(normalized);
		}

		for (const auto& directory : skillDirectories) {
			if (directory.empty()) {
				continue;
			}

			std::error_code ec;
			const std::filesystem::path skillsRoot(directory);
			if (!std::filesystem::exists(skillsRoot, ec) ||
				!std::filesystem::is_directory(skillsRoot, ec)) {
				continue;
			}

			for (const auto& entry : std::filesystem::directory_iterator(skillsRoot, ec)) {
				if (ec || !entry.is_directory()) {
					continue;
				}

				const std::string dirName = entry.path().filename().string();
				if (IsTransientSkillDirectoryName(dirName)) {
					continue;
				}

				const std::filesystem::path manifestPath = entry.path() / "tool-manifest.json";
				if (std::filesystem::exists(manifestPath, ec) &&
					std::filesystem::is_regular_file(manifestPath, ec)) {
					continue;
				}

				const std::string lowerName = ToLowerCopy(dirName);
				const auto toolsIt = toolsBySkill.find(lowerName);
				if (toolsIt == toolsBySkill.end() || toolsIt->second.empty()) {
					continue;
				}

				auto toolEntries = toolsIt->second;
				std::sort(
					toolEntries.begin(),
					toolEntries.end(),
					[](const ToolCatalogEntry& left, const ToolCatalogEntry& right) {
						return left.id < right.id;
					});

				std::string manifest = "{\"tools\":[";
				for (std::size_t index = 0; index < toolEntries.size(); ++index) {
					if (index > 0) {
						manifest += ",";
					}

					const auto& tool = toolEntries[index];
					manifest +=
						"{\"id\":\"" + JsonEscape(tool.id) +
						"\",\"label\":\"" + JsonEscape(tool.label.empty() ? tool.id : tool.label) +
						"\",\"category\":\"" + JsonEscape(tool.category.empty() ? "skill" : tool.category) +
						"\",\"enabled\":" + std::string(tool.enabled ? "true" : "false") + "}";
				}
				manifest += "]}";

				std::ofstream out(manifestPath, std::ios::binary);
				if (!out.is_open()) {
					++m_skillToolSourceDiagnostics.manifestGenerationFailed;
					continue;
				}
				out.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
				if (!out.good()) {
					++m_skillToolSourceDiagnostics.manifestGenerationFailed;
					continue;
				}
				++m_skillToolSourceDiagnostics.manifestsGenerated;
			}
		}

		std::size_t manifestRegistered = 0;
		for (const auto& directory : skillDirectories) {
			manifestRegistered += LoadSkillToolsFromDirectory(directory);
		}

		std::size_t catalogFallbackRegistered = 0;
		for (const auto& [toolId, catalogEntry] : m_catalogSkillTools) {
			if (toolId.empty()) {
				++m_skillToolSourceDiagnostics.catalogRejected;
				continue;
			}

			if (m_tools.find(toolId) != m_tools.end()) {
				continue;
			}

			m_tools.insert_or_assign(toolId, catalogEntry);
			++catalogFallbackRegistered;
		}
		m_skillToolSourceDiagnostics.catalogRegistered += catalogFallbackRegistered;
		return manifestRegistered + catalogFallbackRegistered;
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

		const std::string normalizedRoot = NormalizeSkillDirectoryPath(skillsRoot);
		const std::uint64_t fingerprint = BuildSkillDirectoryManifestFingerprint(skillsRoot);
		const auto cacheIt = m_skillDirectoryLoadCache.find(normalizedRoot);
		if (cacheIt != m_skillDirectoryLoadCache.end() &&
			cacheIt->second.fingerprint == fingerprint) {
			m_skillToolSourceDiagnostics.manifestRegistered += cacheIt->second.loadedCount;
			return cacheIt->second.loadedCount;
		}

		std::size_t registered = 0;
		for (const auto& entry : std::filesystem::directory_iterator(skillsRoot, ec)) {
			if (ec || !entry.is_directory()) {
				continue;
			}

			const std::string dirName = entry.path().filename().string();
			if (IsTransientSkillDirectoryName(dirName)) {
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
					++m_skillToolSourceDiagnostics.manifestRejected;
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

		m_skillDirectoryLoadCache.insert_or_assign(
			normalizedRoot,
			SkillDirectoryLoadSnapshot{
				.fingerprint = fingerprint,
				.loadedCount = registered,
			});
		m_skillToolSourceDiagnostics.manifestRegistered += registered;
		return registered;
	}

	SkillToolSourceDiagnostics GatewayToolRegistry::GetSkillToolSourceDiagnostics() const {
		return m_skillToolSourceDiagnostics;
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
