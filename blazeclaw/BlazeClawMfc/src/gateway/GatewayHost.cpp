#include "pch.h"
#include "GatewayHost.h"

#include "GatewayJsonUtils.h"
#include "GatewayPersistencePaths.h"
#include "GatewayProtocolCodec.h"
#include "GatewayProtocolSchemaValidator.h"
#include "PluginHostAdapter.h"
#include "GatewayHostEx.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "python/PythonRuntimeDispatcher.h"
#include "generated/GatewayHandlerCatalog.Generated.h"
#include "Telemetry.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <vector>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace {
		std::string EscapeJson(const std::string& value);

		void EnsureOpsToolsRuntimeRegistered(GatewayToolRegistry& registry) {
			const ToolPreviewResult weatherPreview = registry.Preview("weather.lookup");
			const ToolPreviewResult emailPreview = registry.Preview("email.schedule");
			if (weatherPreview.allowed && emailPreview.allowed) {
				return;
			}

			const auto loadResult = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
			if (!loadResult.ok) {
				return;
			}

			const auto weatherResolve =
				PluginHostAdapter::ResolveExecutor("ops-tools", "weather.lookup", "");
			if (weatherResolve.resolved && weatherResolve.executor) {
				registry.RegisterRuntimeTool(
					ToolCatalogEntry{
						.id = "weather.lookup",
						.label = "Weather Lookup",
						.category = "data",
						.enabled = true,
					},
					weatherResolve.executor);
			}

			const auto emailResolve =
				PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
			if (emailResolve.resolved && emailResolve.executor) {
				registry.RegisterRuntimeTool(
					ToolCatalogEntry{
						.id = "email.schedule",
						.label = "Email Schedule",
						.category = "communication",
						.enabled = true,
					},
					emailResolve.executor);
			}
		}

		std::string ResolveExtensionsCatalogPath() {
			const std::filesystem::path preferred =
				std::filesystem::path("blazeclaw") /
				"extensions" /
				"extensions.catalog.json";
			if (std::filesystem::exists(preferred)) {
				return preferred.string();
			}

			const std::filesystem::path fallback =
				std::filesystem::path("extensions") /
				"extensions.catalog.json";
			if (std::filesystem::exists(fallback)) {
				return fallback.string();
			}

			char modulePathBuffer[MAX_PATH] = {};
			const DWORD moduleChars =
				GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
			if (moduleChars == 0 || moduleChars >= MAX_PATH) {
				return preferred.string();
			}

			const std::filesystem::path exeDir =
				std::filesystem::path(modulePathBuffer).parent_path();

			const std::vector<std::filesystem::path> relativeCandidates = {
				std::filesystem::path("..") / ".." / "extensions" / "extensions.catalog.json",
				std::filesystem::path("..") / ".." / ".." / "extensions" / "extensions.catalog.json",
				std::filesystem::path("..") / "blazeclaw" / "extensions" / "extensions.catalog.json",
			};

			for (const auto& relativeCandidate : relativeCandidates) {
				const std::filesystem::path candidate =
					std::filesystem::weakly_canonical(exeDir / relativeCandidate);
				if (std::filesystem::exists(candidate)) {
					return candidate.string();
				}
			}

			return preferred.string();
		}

		constexpr const char* kSeedProviderId = "seed";
		constexpr const char* kDeepSeekProviderId = "deepseek";
		constexpr const char* kDefaultModelId = "default";
		constexpr const char* kReasonerModelId = "reasoner";
		constexpr const char* kDeepSeekChatModelId = "deepseek/deepseek-chat";
		constexpr const char* kDeepSeekReasonerModelId = "deepseek/deepseek-reasoner";

		std::string ReadEnvironmentVariable(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			size_t size = 0;
			if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		std::string MaskSecret(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			if (value.size() <= 6) {
				return "***";
			}

			return value.substr(0, 3) + "***" + value.substr(value.size() - 2);
		}

		bool IsDeepSeekModelId(const std::string& modelId) {
			return modelId == kDeepSeekChatModelId ||
				modelId == kDeepSeekReasonerModelId ||
				modelId == kDeepSeekProviderId;
		}

		std::string NormalizeModelId(const std::string& modelId) {
			if (modelId == kDeepSeekProviderId) {
				return kDeepSeekChatModelId;
			}

			return modelId.empty() ? kDefaultModelId : modelId;
		}

		std::string ResolveModelProvider(const std::string& modelId) {
			return IsDeepSeekModelId(modelId) ? kDeepSeekProviderId : kSeedProviderId;
		}

		std::string ResolveModelDisplayName(const std::string& modelId) {
			if (modelId == kReasonerModelId) {
				return "Reasoner Model";
			}

			if (modelId == kDeepSeekChatModelId || modelId == kDeepSeekProviderId) {
				return "DeepSeek Chat";
			}

			if (modelId == kDeepSeekReasonerModelId) {
				return "DeepSeek Reasoner";
			}

			return "Default Model";
		}

		bool ResolveModelStreaming(const std::string& modelId) {
			if (modelId == kReasonerModelId) {
				return false;
			}

			return true;
		}

		std::string BuildModelJson(const std::string& requestedModelId) {
			const std::string modelId = NormalizeModelId(requestedModelId);
			return "{\"id\":\"" + EscapeJson(modelId) +
				"\",\"provider\":\"" + EscapeJson(ResolveModelProvider(modelId)) +
				"\",\"displayName\":\"" + EscapeJson(ResolveModelDisplayName(modelId)) +
				"\",\"streaming\":" +
				std::string(ResolveModelStreaming(modelId) ? "true" : "false") + "}";
		}

		std::string BuildDeepSeekConfigJson(
			const std::string& apiKey,
			const std::string& baseUrl,
			const std::string& defaultModel) {
			return "{\"configured\":" + std::string(!apiKey.empty() ? "true" : "false") +
				",\"apiKeyMasked\":\"" + EscapeJson(MaskSecret(apiKey)) +
				"\",\"baseUrl\":\"" + EscapeJson(baseUrl) +
				"\",\"defaultModel\":\"" + EscapeJson(defaultModel) + "\"}";
		}

		std::string ToNarrow(const std::wstring& value) {
			std::string result;
			result.reserve(value.size());

			for (const wchar_t ch : value) {
				result.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return result;
		}

		std::string EscapeJson(const std::string& value) {
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

		std::optional<std::string> ExtractObjectParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			std::string raw;
			if (!json::FindRawField(paramsJson.value(), fieldName, raw)) {
				return std::nullopt;
			}

			if (!json::IsJsonObjectShape(raw)) {
				return std::nullopt;
			}

			return raw;
		}

		std::optional<bool> ExtractBooleanParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			bool value = false;
			if (!json::FindBoolField(paramsJson.value(), fieldName, value)) {
				return std::nullopt;
			}

			return value;
		}

		std::string SerializeSession(const SessionEntry& session) {
			return "{\"id\":\"" + EscapeJson(session.id) + "\",\"scope\":\"" + EscapeJson(session.scope) +
				"\",\"active\":" + std::string(session.active ? "true" : "false") + "}";
		}

		std::string SerializeAgent(const AgentEntry& agent) {
			return "{\"id\":\"" + EscapeJson(agent.id) + "\",\"name\":\"" + EscapeJson(agent.name) +
				"\",\"active\":" + std::string(agent.active ? "true" : "false") + "}";
		}

		std::string SerializeAgentFile(const AgentFileEntry& file) {
			return "{\"path\":\"" + EscapeJson(file.path) + "\",\"size\":" + std::to_string(file.size) +
				",\"updatedMs\":" + std::to_string(file.updatedMs) + "}";
		}

		std::string SerializeAgentFileContent(const AgentFileContentEntry& file) {
			return "{\"path\":\"" + EscapeJson(file.path) + "\",\"size\":" + std::to_string(file.size) +
				",\"updatedMs\":" + std::to_string(file.updatedMs) +
				",\"content\":\"" + EscapeJson(file.content) + "\"}";
		}

		std::string SerializeChannelStatus(const ChannelStatusEntry& channel) {
			return "{\"id\":\"" + EscapeJson(channel.id) + "\",\"label\":\"" + EscapeJson(channel.label) +
				"\",\"connected\":" + std::string(channel.connected ? "true" : "false") +
				",\"accounts\":" + std::to_string(channel.accountCount) + "}";
		}

		std::string SerializeChannelAccount(const ChannelAccountEntry& account) {
			return "{\"channel\":\"" + EscapeJson(account.channel) + "\",\"accountId\":\"" +
				EscapeJson(account.accountId) + "\",\"label\":\"" + EscapeJson(account.label) +
				"\",\"active\":" + std::string(account.active ? "true" : "false") +
				",\"connected\":" + std::string(account.connected ? "true" : "false") + "}";
		}

		std::string SerializeChannelRoute(const ChannelRouteEntry& route) {
			return "{\"channel\":\"" + EscapeJson(route.channel) + "\",\"accountId\":\"" +
				EscapeJson(route.accountId) + "\",\"agentId\":\"" + EscapeJson(route.agentId) +
				"\",\"sessionId\":\"" + EscapeJson(route.sessionId) + "\"}";
		}

		std::string SerializeTool(const ToolCatalogEntry& tool) {
			const auto dot = tool.id.find('.');
			const std::string derivedSkillKey =
				dot == std::string::npos
				? tool.id
				: tool.id.substr(0, dot);
			const std::string skillKey =
				tool.skillKey.empty() ? derivedSkillKey : tool.skillKey;
			const std::string installKind =
				tool.installKind.empty()
				? (tool.category == "extension"
					? "runtime-registered"
					: tool.category)
				: tool.installKind;
			const std::string source =
				tool.source.empty()
				? "runtime.tool.registry"
				: tool.source;
			return "{\"id\":\"" + EscapeJson(tool.id) + "\",\"label\":\"" + EscapeJson(tool.label) +
				"\",\"category\":\"" + EscapeJson(tool.category) +
				"\",\"skillKey\":\"" + EscapeJson(skillKey) +
				"\",\"installKind\":\"" + EscapeJson(installKind) +
				"\",\"source\":\"" + EscapeJson(source) + "\",\"enabled\":" +
				std::string(tool.enabled ? "true" : "false") + "}";
		}

		std::string SerializeTaskDeltaEntry(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
			return "{\"index\":" + std::to_string(delta.index) +
				",\"schemaVersion\":" + std::to_string(delta.schemaVersion) +
				",\"runId\":\"" + EscapeJson(delta.runId) +
				"\",\"sessionId\":\"" + EscapeJson(delta.sessionId) +
				"\",\"phase\":\"" + EscapeJson(delta.phase) +
				"\",\"toolName\":\"" + EscapeJson(delta.toolName) +
				"\",\"fallbackBackend\":\"" + EscapeJson(delta.fallbackBackend) +
				"\",\"fallbackAction\":\"" + EscapeJson(delta.fallbackAction) +
				"\",\"fallbackAttempt\":" + std::to_string(delta.fallbackAttempt) +
				",\"fallbackMaxAttempts\":" + std::to_string(delta.fallbackMaxAttempts) +
				",\"argsJson\":\"" + EscapeJson(delta.argsJson) +
				"\",\"resultJson\":\"" + EscapeJson(delta.resultJson) +
				"\",\"status\":\"" + EscapeJson(delta.status) +
				"\",\"errorCode\":\"" + EscapeJson(delta.errorCode) +
				"\",\"startedAtMs\":" + std::to_string(delta.startedAtMs) +
				",\"completedAtMs\":" + std::to_string(delta.completedAtMs) +
				",\"latencyMs\":" + std::to_string(delta.latencyMs) +
				",\"modelTurnId\":\"" + EscapeJson(delta.modelTurnId) +
				"\",\"stepLabel\":\"" + EscapeJson(delta.stepLabel) + "\"}";
		}

		std::string SerializeTaskDeltaState(
			const std::unordered_map<std::string, std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>>& state) {
			std::vector<std::string> runIds;
			runIds.reserve(state.size());
			for (const auto& [runId, _] : state) {
				runIds.push_back(runId);
			}

			std::sort(runIds.begin(), runIds.end());

			std::string json = "{\"runs\":[";
			bool firstRun = true;
			for (const auto& runId : runIds) {
				const auto it = state.find(runId);
				if (it == state.end()) {
					continue;
				}

				if (!firstRun) {
					json += ",";
				}
				firstRun = false;

				json += "{\"runId\":\"" + EscapeJson(runId) + "\",\"taskDeltas\":[";
				for (std::size_t i = 0; i < it->second.size(); ++i) {
					if (i > 0) {
						json += ",";
					}

					json += SerializeTaskDeltaEntry(it->second[i]);
				}
				json += "]}";
			}

			json += "]}";
			return json;
		}

		GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizePersistedTaskDelta(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionId,
			const std::size_t stableIndex) {
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry normalized = source;
			normalized.index = stableIndex;
			if (normalized.runId.empty()) {
				normalized.runId = runId;
			}

			if (normalized.sessionId.empty()) {
				normalized.sessionId = sessionId.empty() ? "main" : sessionId;
			}

			if (normalized.phase.empty()) {
				normalized.phase = stableIndex + 1 == 1 ? "plan" : "unknown";
			}

			if (normalized.phase == "final") {
				if (normalized.status.empty()) {
					normalized.status = "completed";
				}
				if (normalized.stepLabel.empty()) {
					normalized.stepLabel = "run_terminal";
				}
			}
			else {
				if (normalized.status.empty()) {
					normalized.status = "ok";
				}
				if (normalized.stepLabel.empty()) {
					normalized.stepLabel = normalized.phase;
				}
			}

			if (normalized.startedAtMs == 0 && normalized.completedAtMs > 0) {
				normalized.startedAtMs = normalized.completedAtMs;
			}
			if (normalized.completedAtMs == 0 && normalized.startedAtMs > 0) {
				normalized.completedAtMs = normalized.startedAtMs;
			}
			if (normalized.completedAtMs < normalized.startedAtMs) {
				normalized.completedAtMs = normalized.startedAtMs;
			}
			normalized.latencyMs =
				normalized.completedAtMs >= normalized.startedAtMs
				? (normalized.completedAtMs - normalized.startedAtMs)
				: 0;

			return normalized;
		}

		std::string SerializeToolExecution(const ToolExecutionEntry& execution) {
			return "{\"tool\":\"" + EscapeJson(execution.tool) + "\",\"executed\":" +
				std::string(execution.executed ? "true" : "false") +
				",\"status\":\"" + EscapeJson(execution.status) + "\",\"output\":\"" +
				EscapeJson(execution.output) + "\",\"argsProvided\":" +
				std::string(execution.argsProvided ? "true" : "false") + "}";
		}

		std::string SerializeChannelAdapter(const ChannelAdapterDescriptor& adapter) {
			return "{\"id\":\"" + EscapeJson(adapter.id) + "\",\"label\":\"" +
				EscapeJson(adapter.label) + "\",\"defaultAccountId\":\"" +
				EscapeJson(adapter.defaultAccountId) + "\"}";
		}

		std::string SerializeStringArray(const std::vector<std::string>& values) {
			std::string json = "[";
			for (std::size_t i = 0; i < values.size(); ++i) {
				if (i > 0) {
					json += ",";
				}

				json += "\"" + EscapeJson(values[i]) + "\"";
			}

			json += "]";
			return json;
		}

		const std::vector<std::string>& EventCatalogNames() {
			static const std::vector<std::string> events = {
				"gateway.agent.update",
				"gateway.channels.accounts.update",
				"gateway.channels.update",
				"gateway.health",
				"gateway.session.reset",
				"gateway.shutdown",
				"gateway.tick",
				"gateway.tools.catalog.update",
			};

			return events;
		}

		std::string ExtractStringParam(const std::optional<std::string>& paramsJson, const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			std::string value;
			if (!json::FindStringField(paramsJson.value(), fieldName, value)) {
				return {};
			}

			return value;
		}

		std::optional<std::size_t> ExtractNumericParam(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			std::uint64_t value = 0;
			if (!json::FindUInt64Field(paramsJson.value(), fieldName, value)) {
				return std::nullopt;
			}

			return static_cast<std::size_t>(value);
		}

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string TruncateForMatch(const std::string& text, std::size_t maxChars) {
			if (text.size() <= maxChars) {
				return text;
			}

			return text.substr(0, maxChars) + "...";
		}

		std::string BuildMemorySearchEnvelope(
			const std::string& sessionKey,
			const std::vector<std::string>& matches) {
			std::string matchesJson = "[";
			for (std::size_t i = 0; i < matches.size(); ++i) {
				if (i > 0) {
					matchesJson += ",";
				}

				matchesJson += "{\"text\":\"" +
					EscapeJson(matches[i]) +
					"\"}";
			}
			matchesJson += "]";

			return std::string("{\"sessionKey\":\"") +
				EscapeJson(sessionKey) +
				"\",\"matches\":" +
				matchesJson +
				",\"count\":" +
				std::to_string(matches.size()) +
				"}";
		}

		bool IsUnsafeAgentFilePath(const std::string& path) {
			if (path.empty()) {
				return true;
			}

			if (path.find("..") != std::string::npos) {
				return true;
			}

			if (path.find('\\') != std::string::npos ||
				path.find(':') != std::string::npos) {
				return true;
			}

			if (!path.empty() && path.front() == '/') {
				return true;
			}

			return false;
		}

	} // namespace

	bool GatewayHost::StartLocalDispatchOnly() {
		if (m_dispatchInitialized) {
			m_running = true;
			return true;
		}

		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		RegisterDefaultHandlers();
		m_dispatchInitialized = true;
		m_runtimeHandlersInitialized = true;
		m_running = true;
		return true;
	}

	bool GatewayHost::StartLocalRuntimeDispatchOnly() {
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills");
		m_dispatcher.Register("gateway.tools.list", [this](const protocol::RequestFrame& request) {
			const std::string category = ExtractStringParam(request.paramsJson, "category");
			const auto tools = m_toolRegistry.List();
			std::string toolsJson = "[";
			std::size_t count = 0;
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (!category.empty() && tools[i].category != category) {
					continue;
				}
				if (count > 0) {
					toolsJson += ",";
				}
				toolsJson += SerializeTool(tools[i]);
				++count;
			}
			toolsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tools\":" + toolsJson + ",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});
		m_dispatcher.Register("gateway.tools.catalog", [this](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			std::string toolsJson = "[";
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (i > 0) {
					toolsJson += ",";
				}
				toolsJson += SerializeTool(tools[i]);
			}
			toolsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tools\":" + toolsJson + "}",
				.error = std::nullopt,
			};
			});
		if (!m_runtimeHandlersInitialized) {
			RegisterRuntimeHandlers();
			m_runtimeHandlersInitialized = true;
		}
		m_dispatchInitialized = true;

		m_running = true;
		return true;
	}

	bool GatewayHost::InitializeRuntime(const blazeclaw::config::GatewayConfig& config) {
		if (!m_dispatchInitialized) {
			RegisterDefaultHandlers();
			m_dispatchInitialized = true;
		}
		if (!m_stageRuntimeHost) {
			m_stageRuntimeHost = std::make_unique<GatewayHostEx>(
				GatewayHostExDependencies{
					.legacyHost = this,
					.stagePipeline = &m_chatRunPipelineOrchestrator,
				});
		}
		if (m_initialized) {
			return true;
		}

		if (config.bindAddress.empty() || config.port == 0) {
			m_lastWarning = "Invalid gateway bind configuration.";
			return false;
		}

		m_bindAddress = ToNarrow(config.bindAddress);
		m_port = config.port;
		m_runtimeGatewayBind = m_bindAddress;
		m_runtimeGatewayPort = m_port;
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		m_runtimeDeepSeekApiKey = ReadEnvironmentVariable("DEEPSEEK_API_KEY");
		const std::string deepSeekBaseUrl =
			ReadEnvironmentVariable("DEEPSEEK_BASE_URL");
		if (!deepSeekBaseUrl.empty()) {
			m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
		}

		if (!m_runtimeDeepSeekApiKey.empty() &&
			(m_runtimeAgentModel == kDefaultModelId ||
				m_runtimeAgentModel == kReasonerModelId)) {
			m_runtimeAgentModel = m_runtimeDeepSeekDefaultModel;
		}

		const std::string catalogPath = ResolveExtensionsCatalogPath();
		m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills-bundled");
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills-openclaw-original");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills-openclaw-original");
		// Activate lifecycle-managed extensions (register tools without executors).
		m_extensionLifecycle.LoadCatalog(catalogPath);
		m_extensionLifecycle.ActivateAll(m_toolRegistry);
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		m_approvalStore.Initialize(ResolveGatewayStateFilePath("approvals.json").string());
		LoadPersistedTaskDeltas();
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "python.script.run",
				.label = "Python Script Run",
				.category = "runtime",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateExecutor());
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "python.runtime.health",
				.label = "Python Runtime Health",
				.category = "runtime",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateDiagnosticsExecutor());
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "chat.send",
				.label = "Chat Send",
				.category = "messaging",
				.enabled = true,
			},
			[this](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
				const protocol::RequestFrame runtimeRequest{
					.id = "tool-runtime-" + std::to_string(CurrentEpochMs()),
					.method = requestedTool,
					.paramsJson = argsJson,
				};

				const protocol::ResponseFrame runtimeResponse = RouteRequest(runtimeRequest);
				if (!runtimeResponse.ok) {
					const std::string detail = runtimeResponse.error.has_value()
						? (runtimeResponse.error->code + ":" + runtimeResponse.error->message)
						: "runtime_execution_failed";
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "error",
						.output = detail,
					};
				}

				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = true,
					.status = "ok",
					.output = runtimeResponse.payloadJson.value_or("{}"),
				};
			});
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "memory.search",
				.label = "Memory Search",
				.category = "knowledge",
				.enabled = true,
			},
			[this](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
				if (!argsJson.has_value()) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = "missing_args",
					};
				}

				std::string query;
				std::string sessionKey;
				json::FindStringField(argsJson.value(), "query", query);
				json::FindStringField(argsJson.value(), "sessionKey", sessionKey);
				if (json::Trim(query).empty()) {
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "invalid_args",
						.output = "query_required",
					};
				}

				std::uint64_t requestedLimit = 3;
				json::FindUInt64Field(argsJson.value(), "limit", requestedLimit);
				const std::size_t limit = static_cast<std::size_t>((std::max)(
					std::uint64_t{ 1 },
					(std::min)(requestedLimit, std::uint64_t{ 10 })));

				const std::string normalizedSession =
					json::Trim(sessionKey).empty() ? "main" : json::Trim(sessionKey);
				const std::string loweredQuery = ToLowerCopy(query);

				std::vector<std::string> matches;
				const auto historyIt = m_chatHistoryBySession.find(normalizedSession);
				if (historyIt != m_chatHistoryBySession.end()) {
					for (const auto& messageJson : historyIt->second) {
						if (ToLowerCopy(messageJson).find(loweredQuery) == std::string::npos) {
							continue;
						}

						matches.push_back(TruncateForMatch(messageJson, 180));
						if (matches.size() >= limit) {
							break;
						}
					}
				}

				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = true,
					.status = "ok",
					.output = BuildMemorySearchEnvelope(normalizedSession, matches),
				};
			});

		m_fixtureParityValidated = false;
		m_stageRuntimeHost.reset();

		// Emit telemetry about startup configuration (mask sensitive values)
		{
			const std::string payload =
				"{\"bind\":" +
				JsonString(m_runtimeGatewayBind) +
				",\"port\":" +
				std::to_string(m_runtimeGatewayPort) +
				",\"agentModel\":" +
				JsonString(m_runtimeAgentModel) +
				"}";
			EmitTelemetryEvent("gateway.startup.config", payload);
		}

		m_initialized = true;
		return true;
	}

	bool GatewayHost::Start(const blazeclaw::config::GatewayConfig& config) {
		if (m_running) {
			return true;
		}
		if (!InitializeRuntime(config)) {
			return false;
		}

		std::string transportError;
		if (!m_transport.Start(
			m_bindAddress,
			m_port,
			[this](const std::string& inboundFrame) {
				return HandleInboundText(inboundFrame);
			},
			transportError)) {
			m_lastWarning = transportError;
			return false;
		}

		m_running = true;
		return true;
	}

	bool GatewayHost::StartLocalOnly(const blazeclaw::config::GatewayConfig& config) {
		if (!InitializeRuntime(config)) {
			return false;
		}

		m_running = true;
		return true;
	}

	void GatewayHost::Stop() {
		m_transport.Stop();
		// Deactivate registered extension tools and clear approval state
		m_extensionLifecycle.DeactivateAll(m_toolRegistry);
		PersistTaskDeltas();
		m_running = false;
		m_initialized = false;
		m_dispatchInitialized = false;
		m_runtimeHandlersInitialized = false;
		m_fixtureParityValidated = false;
		m_bindAddress.clear();
		m_port = 0;
	}

	void GatewayHost::SetSkillsCatalogState(SkillsCatalogGatewayState state) {
		m_skillsCatalogState = std::move(state);
	}

	void GatewayHost::SetSkillsRefreshCallback(
		SkillsRefreshCallback callback) {
		m_skillsRefreshCallback = std::move(callback);
	}

	void GatewayHost::SetSkillsUpdateCallback(
		SkillsUpdateCallback callback) {
		m_skillsUpdateCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddedOrchestrationPath(
		const std::string& path) {
		const std::string normalized = ToLowerCopy(json::Trim(path));
		m_stagePipelineFeatureEnabled = true;
		m_stagePipelineRolloutCohort = "default";
		if (normalized == "runtime_orchestration") {
			m_embeddedOrchestrationPath = normalized;
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "compat_runtime_orchestration";
			return;
		}
		if (normalized == "legacy_only") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "legacy_only";
			return;
		}
		if (normalized == "stage_pipeline_canary") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = true;
			m_stagePipelineRolloutCohort = "canary";
			return;
		}

		m_embeddedOrchestrationPath = "dynamic_task_delta";
	}

	void GatewayHost::SetEmailFallbackRuntimeFlags(
		bool preflightEnabled,
		bool policyProfilesEnabled,
		bool policyProfilesEnforce) {
		m_runtimeEmailPreflightEnabled = preflightEnabled;
		m_runtimeEmailPolicyProfilesEnabled = policyProfilesEnabled;
		m_runtimeEmailPolicyProfilesEnforce = policyProfilesEnforce;
	}

	void GatewayHost::SetEmailFallbackResolvedPolicy(
		const std::vector<std::string>& backends,
		const std::string& onUnavailable,
		const std::string& onAuthError,
		const std::string& onExecError,
		std::uint32_t retryMaxAttempts,
		std::uint32_t retryDelayMs,
		bool requiresApproval,
		std::uint32_t approvalTokenTtlMinutes,
		const std::string& profileId) {
		m_runtimeEmailResolvedBackends = backends;
		m_runtimeEmailPolicyOnUnavailable = onUnavailable;
		m_runtimeEmailPolicyOnAuthError = onAuthError;
		m_runtimeEmailPolicyOnExecError = onExecError;
		m_runtimeEmailRetryMaxAttempts = retryMaxAttempts;
		m_runtimeEmailRetryDelayMs = retryDelayMs;
		m_runtimeEmailRequiresApproval = requiresApproval;
		m_runtimeEmailApprovalTokenTtlMinutes = approvalTokenTtlMinutes;
		m_runtimeEmailPolicyProfileId = profileId;

		if (!m_runtimeEmailResolvedBackends.empty()) {
			std::string joined;
			for (std::size_t i = 0; i < m_runtimeEmailResolvedBackends.size(); ++i) {
				if (i > 0) {
					joined += ",";
				}
				joined += m_runtimeEmailResolvedBackends[i];
			}
			_putenv_s("BLAZECLAW_EMAIL_POLICY_BACKENDS", joined.c_str());
		}
		else {
			_putenv_s("BLAZECLAW_EMAIL_POLICY_BACKENDS", "");
		}

		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE", m_runtimeEmailPolicyOnUnavailable.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_AUTH_ERROR", m_runtimeEmailPolicyOnAuthError.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR", m_runtimeEmailPolicyOnExecError.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_RETRY_MAX_ATTEMPTS", std::to_string(m_runtimeEmailRetryMaxAttempts).c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_RETRY_DELAY_MS", std::to_string(m_runtimeEmailRetryDelayMs).c_str());
		_putenv_s("BLAZECLAW_EMAIL_APPROVAL_TOKEN_TTL_MINUTES", std::to_string(m_runtimeEmailApprovalTokenTtlMinutes).c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_PROFILE_ID", m_runtimeEmailPolicyProfileId.c_str());
	}

	void GatewayHost::LoadPersistedTaskDeltas() {
		const std::filesystem::path persistencePath =
			ResolveGatewayStateFilePath("taskdeltas.state");
		std::ifstream input(persistencePath, std::ios::in | std::ios::binary);
		if (!input.is_open()) {
			return;
		}

		std::ostringstream buffer;
		buffer << input.rdbuf();
		const std::string jsonState = buffer.str();
		if (jsonState.empty()) {
			return;
		}

		if (jsonState.size() > m_taskDeltasMaxPayloadBytes) {
			return;
		}

		nlohmann::json root;
		try {
			root = nlohmann::json::parse(jsonState);
		}
		catch (...) {
			return;
		}

		if (!root.is_object() || !root.contains("runs") || !root["runs"].is_array()) {
			return;
		}

		m_taskDeltaRepository.ClearAll();
		for (const auto& runNode : root["runs"]) {
			if (!runNode.is_object() ||
				!runNode.contains("runId") ||
				!runNode["runId"].is_string() ||
				!runNode.contains("taskDeltas") ||
				!runNode["taskDeltas"].is_array()) {
				continue;
			}

			const std::string runId = runNode["runId"].get<std::string>();
			if (runId.empty()) {
				continue;
			}

			std::vector<ChatRuntimeResult::TaskDeltaEntry> parsedDeltas;
			for (const auto& deltaNode : runNode["taskDeltas"]) {
				if (!deltaNode.is_object()) {
					continue;
				}

				ChatRuntimeResult::TaskDeltaEntry delta;
				delta.index = deltaNode.value("index", std::size_t{ 0 });
				delta.schemaVersion = deltaNode.value("schemaVersion", std::uint32_t{ 1 });
				delta.runId = deltaNode.value("runId", runId);
				delta.sessionId = deltaNode.value("sessionId", std::string{});
				delta.phase = deltaNode.value("phase", std::string{});
				delta.toolName = deltaNode.value("toolName", std::string{});
				delta.fallbackBackend = deltaNode.value("fallbackBackend", std::string{});
				delta.fallbackAction = deltaNode.value("fallbackAction", std::string{});
				delta.fallbackAttempt = deltaNode.value("fallbackAttempt", std::size_t{ 0 });
				delta.fallbackMaxAttempts = deltaNode.value("fallbackMaxAttempts", std::size_t{ 0 });
				delta.argsJson = deltaNode.value("argsJson", std::string{});
				delta.resultJson = deltaNode.value("resultJson", std::string{});
				delta.status = deltaNode.value("status", std::string{});
				delta.errorCode = deltaNode.value("errorCode", std::string{});
				delta.startedAtMs = deltaNode.value("startedAtMs", std::uint64_t{ 0 });
				delta.completedAtMs = deltaNode.value("completedAtMs", std::uint64_t{ 0 });
				delta.latencyMs = deltaNode.value("latencyMs", std::uint64_t{ 0 });
				delta.modelTurnId = deltaNode.value("modelTurnId", std::string{});
				delta.stepLabel = deltaNode.value("stepLabel", std::string{});
				parsedDeltas.push_back(std::move(delta));
			}

			std::sort(
				parsedDeltas.begin(),
				parsedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& left,
					const ChatRuntimeResult::TaskDeltaEntry& right) {
						return left.index < right.index;
				});

			std::string fallbackSessionId;
			for (const auto& delta : parsedDeltas) {
				if (!delta.sessionId.empty()) {
					fallbackSessionId = delta.sessionId;
					break;
				}
			}

			auto normalizedDeltas = TaskDeltaLegacyAdapter::AdaptRun(
				runId,
				fallbackSessionId,
				parsedDeltas);

			const bool hasPlanPhase = std::any_of(
				normalizedDeltas.begin(),
				normalizedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& delta) {
					return delta.phase == "plan";
				});
			if (!hasPlanPhase) {
				normalizedDeltas.insert(
					normalizedDeltas.begin(),
					ChatRuntimeResult::TaskDeltaEntry{
						.index = 0,
						.runId = runId,
						.sessionId = fallbackSessionId.empty() ? "main" : fallbackSessionId,
						.phase = "plan",
						.resultJson = "[]",
						.status = "ok",
						.errorCode = {},
						.startedAtMs = 0,
						.completedAtMs = 0,
						.latencyMs = 0,
						.stepLabel = "execution_plan",
					});
			}

			const bool hasFinalPhase = std::any_of(
				normalizedDeltas.begin(),
				normalizedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& delta) {
					return delta.phase == "final";
				});
			if (!hasFinalPhase) {
				normalizedDeltas.push_back(
					ChatRuntimeResult::TaskDeltaEntry{
						.index = normalizedDeltas.size(),
						.runId = runId,
						.sessionId = fallbackSessionId.empty() ? "main" : fallbackSessionId,
						.phase = "final",
						.resultJson = {},
						.status = "completed",
						.errorCode = {},
						.startedAtMs = 0,
						.completedAtMs = 0,
						.latencyMs = 0,
						.stepLabel = "run_terminal",
					});
			}

			for (std::size_t i = 0; i < normalizedDeltas.size(); ++i) {
				normalizedDeltas[i] = TaskDeltaLegacyAdapter::AdaptEntry(
					normalizedDeltas[i],
					runId,
					fallbackSessionId,
					i);
			}

			std::string schemaErrorCode;
			std::string schemaErrorMessage;
			if (!TaskDeltaSchemaValidator::ValidateRun(
				runId,
				normalizedDeltas,
				schemaErrorCode,
				schemaErrorMessage)) {
				continue;
			}

			const bool upserted = m_taskDeltaRepository.Upsert(
				runId,
				normalizedDeltas);
			(void)upserted;
		}

		while (m_taskDeltaRepository.Size() > m_taskDeltasRetentionLimit) {
			const auto& snapshot = m_taskDeltaRepository.Snapshot();
			if (snapshot.empty()) {
				break;
			}

			const bool cleared =
				m_taskDeltaRepository.Clear(snapshot.begin()->first);
			(void)cleared;
		}

		PersistTaskDeltas();
	}

	void GatewayHost::PersistTaskDeltas() const {
		const std::filesystem::path persistencePath =
			ResolveGatewayStateFilePath("taskdeltas.state");
		std::error_code ec;
		std::filesystem::create_directories(persistencePath.parent_path(), ec);

		const auto& taskDeltaState = m_taskDeltaRepository.Snapshot();
		const std::string jsonState = SerializeTaskDeltaState(taskDeltaState);
		if (jsonState.size() > m_taskDeltasMaxPayloadBytes) {
			return;
		}

		std::ofstream output(
			persistencePath,
			std::ios::out | std::ios::trunc | std::ios::binary);
		if (!output.is_open()) {
			return;
		}

		output.write(jsonState.data(), static_cast<std::streamsize>(jsonState.size()));
	}

	void GatewayHost::SetChatRuntimeCallback(
		ChatRuntimeCallback callback) {
		m_chatRuntimeCallback = std::move(callback);
	}

	void GatewayHost::SetChatAbortCallback(
		ChatAbortCallback callback) {
		m_chatAbortCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddingsGenerateCallback(
		EmbeddingsGenerateCallback callback) {
		m_embeddingsGenerateCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddingsBatchCallback(
		EmbeddingsBatchCallback callback) {
		m_embeddingsBatchCallback = std::move(callback);
	}

	std::vector<ToolCatalogEntry> GatewayHost::ListRuntimeTools() const {
		return m_toolRegistry.List();
	}

	ToolExecuteResult GatewayHost::ExecuteRuntimeTool(
		const std::string& tool,
		const std::optional<std::string>& argsJson) {
		return m_toolRegistry.Execute(tool, argsJson);
	}

	ToolExecuteResultV2 GatewayHost::ExecuteRuntimeToolV2(
		const ToolExecuteRequestV2& request) {
		return m_toolRegistry.ExecuteV2(request);
	}

	void GatewayHost::RegisterRuntimeTool(
		const ToolCatalogEntry& tool,
		GatewayToolRegistry::RuntimeToolExecutor executor) {
		m_toolRegistry.RegisterRuntimeTool(tool, std::move(executor));
	}

	void GatewayHost::RegisterRuntimeToolV2(
		const ToolCatalogEntry& tool,
		GatewayToolRegistry::RuntimeToolExecutorV2 executor) {
		m_toolRegistry.RegisterRuntimeToolV2(tool, std::move(executor));
	}

	bool GatewayHost::IsRunning() const noexcept {
		return m_running;
	}

	std::string GatewayHost::LastWarning() const {
		auto* mutableThis = const_cast<GatewayHost*>(this);
		mutableThis->EnsureFixtureParityValidated();
		return m_lastWarning;
	}

	void GatewayHost::EnsureFixtureParityValidated() {
		if (m_fixtureParityValidated) {
			return;
		}

		m_fixtureParityValidated = true;
		if (!m_lastWarning.empty()) {
			return;
		}

		std::string fixtureError;
		if (!protocol::GatewayProtocolContract::ValidateFixtureParity(
			"blazeclaw/fixtures/gateway",
			fixtureError)) {
			m_lastWarning = fixtureError;
		}
	}

	bool GatewayHost::AcceptConnection(const std::string& connectionId, std::string& error) {
		return m_transport.AcceptConnection(connectionId, error);
	}

	bool GatewayHost::PumpInboundFrame(
		const std::string& connectionId,
		const std::string& inboundFrame,
		std::string& error) {
		return m_transport.ProcessInboundFrame(connectionId, inboundFrame, error);
	}

	std::vector<std::string> GatewayHost::DrainOutboundFrames(
		const std::string& connectionId,
		std::string& error) {
		return m_transport.DrainOutboundFrames(connectionId, error);
	}

	bool GatewayHost::PumpNetworkOnce(std::string& error) {
		if (!m_transport.IsRunning()) {
			error.clear();
			return true;
		}

		return m_transport.PumpNetworkOnce(error);
	}

	std::string GatewayHost::BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.tick",
			.payloadJson = "{\"ts\":" + std::to_string(timestampMs) +
				",\"running\":" + std::string(IsRunning() ? "true" : "false") +
				",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"tick\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const {
		const auto accounts = m_channelRegistry.ListAccounts();
		std::string accountsJson = "[";
		for (std::size_t i = 0; i < accounts.size(); ++i) {
			if (i > 0) {
				accountsJson += ",";
			}

			accountsJson += SerializeChannelAccount(accounts[i]);
		}

		accountsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.channels.accounts.update",
			.payloadJson = "{\"accounts\":" + accountsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"channels.accounts.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const {
		const AgentEntry agent = m_agentRegistry.Get(agentId);

		protocol::EventFrame frame{
			.eventName = "gateway.agent.update",
			.payloadJson = "{\"agentId\":\"" + agent.id + "\",\"agent\":" + SerializeAgent(agent) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"agent.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const {
		const auto tools = m_toolRegistry.List();
		std::string toolsJson = "[";
		for (std::size_t i = 0; i < tools.size(); ++i) {
			if (i > 0) {
				toolsJson += ",";
			}

			toolsJson += SerializeTool(tools[i]);
		}

		toolsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.tools.catalog.update",
			.payloadJson = "{\"tools\":" + toolsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"tools.catalog.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildChannelsUpdateEventFrame(std::uint64_t seq) const {
		const auto channels = m_channelRegistry.ListStatus();
		std::string channelsJson = "[";
		for (std::size_t i = 0; i < channels.size(); ++i) {
			if (i > 0) {
				channelsJson += ",";
			}

			channelsJson += SerializeChannelStatus(channels[i]);
		}

		channelsJson += "]";

		protocol::EventFrame frame{
			.eventName = "gateway.channels.update",
			.payloadJson = "{\"channels\":" + channelsJson + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"channels.update\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const {
		const SessionEntry session = m_sessionRegistry.Resolve(sessionId);

		protocol::EventFrame frame{
			.eventName = "gateway.session.reset",
			.payloadJson = "{\"sessionId\":\"" + session.id + "\",\"session\":" + SerializeSession(session) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"session.reset\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildHealthEventFrame(std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.health",
			.payloadJson = "{\"status\":\"ok\",\"running\":true}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"health\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const {
		protocol::EventFrame frame{
			.eventName = "gateway.shutdown",
			.payloadJson = "{\"reason\":\"" + reason + "\",\"graceful\":true,\"seq\":" + std::to_string(seq) + "}",
			.seq = seq,
			.stateVersion = seq,
		};

		protocol::SchemaValidationIssue issue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateEvent(frame, issue)) {
			frame = {
				.eventName = "gateway.schema.error",
				.payloadJson = "{\"stage\":\"shutdown\",\"message\":\"event validation failed\"}",
				.seq = seq,
				.stateVersion = seq,
			};
		}

		return protocol::EncodeEventFrame(frame);
	}

	std::string GatewayHost::HandleInboundText(const std::string& inboundJson) const {
		protocol::RequestFrame request;
		std::string decodeError;
		if (!protocol::TryDecodeRequestFrame(inboundJson, request, decodeError)) {
			const protocol::ResponseFrame errorResponse{
				.id = "",
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = "invalid_frame",
					.message = decodeError,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(errorResponse);
		}

		protocol::SchemaValidationIssue validationIssue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateRequest(request, validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
					.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		const protocol::ResponseFrame routedResponse = RouteRequest(request);
		if (!protocol::GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			request.method,
			routedResponse,
			validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_invalid_response" : validationIssue.code,
					.message = validationIssue.message.empty()
						? "Handler response failed schema validation."
						: validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		return protocol::EncodeResponseFrame(routedResponse);
	}

	protocol::ResponseFrame GatewayHost::RouteRequest(const protocol::RequestFrame& request) const {
		if (request.method != "chat.send") {
			return RouteRequestLegacy(request);
		}

		auto* mutableThis = const_cast<GatewayHost*>(this);
		if (mutableThis->m_stageRuntimeHost == nullptr) {
			mutableThis->m_stageRuntimeHost = std::make_unique<GatewayHostEx>(
				GatewayHostExDependencies{
					.legacyHost = this,
					.stagePipeline = &m_chatRunPipelineOrchestrator,
				});
		}

		const bool stageHealthy =
			mutableThis->m_stageRuntimeHost != nullptr &&
			mutableThis->m_stageRuntimeHost->IsHealthy();
		const GatewayHostRouteRequest routeRequest{
			  .method = request.method,
			  .orchestrationPath = m_embeddedOrchestrationPath,
			  .stageHostHealthy = stageHealthy,
			  .runtimeOrchestrationCompatEnabled =
				  m_embeddedOrchestrationPath == "runtime_orchestration",
			.stagePipelineFeatureEnabled = m_stagePipelineFeatureEnabled,
			  .rolloutCohort = m_stagePipelineRolloutCohort,
		};
		const GatewayHostRouteDecision routeDecision =
			m_hostRouter.Decide(routeRequest);
		EmitTelemetryEvent(
			"gateway.host.route.decision",
			BuildGatewayHostRouteDecisionPayload(
				request.method,
				routeDecision.target == GatewayHostRouteTarget::StagePipeline
				? "stage_pipeline"
				: "legacy",
				routeDecision.reasonCode,
				routeDecision.selectedCohort,
				routeDecision.fallback));

		if (routeDecision.target == GatewayHostRouteTarget::StagePipeline &&
			mutableThis->m_stageRuntimeHost != nullptr) {
			return mutableThis->m_stageRuntimeHost->RouteRequest(request);
		}

		return RouteRequestLegacy(request);
	}

	protocol::ResponseFrame GatewayHost::RouteRequestLegacy(
		const protocol::RequestFrame& request) const {
		return m_dispatcher.Dispatch(request);
	}

	bool GatewayHost::IsHealthy() const noexcept {
		return m_dispatchInitialized;
	}

	void GatewayHost::RegisterDefaultHandlers() {
		RegisterChannelsHandlers();

		RegisterEventHandlers();

		m_dispatcher.Register("gateway.tools.executions.list", [this](const protocol::RequestFrame& request) {
			const auto executions = m_toolRegistry.ListExecutions(20);
			std::string executionsJson = "[";
			for (std::size_t i = 0; i < executions.size(); ++i) {
				if (i > 0) {
					executionsJson += ",";
				}
				executionsJson += SerializeToolExecution(executions[i]);
			}
			executionsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"executions\":" + executionsJson + ",\"count\":" + std::to_string(executions.size()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.executions.count", [this](const protocol::RequestFrame& request) {
			const ToolExecutionStats stats = m_toolRegistry.GetExecutionStats();
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"count\":" + std::to_string(stats.count) +
					",\"succeeded\":" + std::to_string(stats.succeeded) +
					",\"failed\":" + std::to_string(stats.failed) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.executions.latest", [this](const protocol::RequestFrame& request) {
			const std::optional<ToolExecutionEntry> latest = m_toolRegistry.LatestExecution();
			const ToolExecutionEntry fallback = ToolExecutionEntry{
				.tool = "none",
				.executed = false,
				.status = "empty",
				.output = "no_history",
				.argsProvided = false,
			};

			const ToolExecutionEntry& selected = latest.has_value() ? latest.value() : fallback;
			const ToolExecutionStats stats = m_toolRegistry.GetExecutionStats();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"found\":" + std::string(latest.has_value() ? "true" : "false") +
					",\"execution\":" + SerializeToolExecution(selected) +
					",\"count\":" + std::to_string(stats.count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.executions.clear", [this](const protocol::RequestFrame& request) {
			const std::size_t cleared = m_toolRegistry.ClearExecutions();
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"cleared\":" + std::to_string(cleared) +
					",\"remaining\":0}",
				.error = std::nullopt,
			};
			});



		RegisterToolsHandlers();

		RegisterGeneratedScopeClusterHandlers();

		m_dispatcher.Register("gateway.events.latestByType", [](const protocol::RequestFrame& request) {
			const std::string type = ExtractStringParam(request.paramsJson, "type");
			const bool lifecycle = type == "lifecycle";
			const std::string event = lifecycle ? "gateway.shutdown" : "gateway.tools.catalog.update";
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"type\":\"" + EscapeJson(type.empty() ? "update" : type) + "\",\"event\":\"" + EscapeJson(event) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.snapshot", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			 .payloadJson = "{\"gateway\":{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) +
					"\",\"port\":" + std::to_string(m_runtimeGatewayPort) + "},\"agent\":{\"model\":\"" +
					EscapeJson(m_runtimeAgentModel) + "\",\"streaming\":" +
					std::string(m_runtimeAgentStreaming ? "true" : "false") + "},\"emailFallback\":{\"preflightEnabled\":" +
					std::string(m_runtimeEmailPreflightEnabled ? "true" : "false") +
					",\"policyProfilesEnabled\":" +
					std::string(m_runtimeEmailPolicyProfilesEnabled ? "true" : "false") +
					",\"policyProfilesEnforce\":" +
					std::string(m_runtimeEmailPolicyProfilesEnforce ? "true" : "false") +
					"},\"deepseek\":" +
					BuildDeepSeekConfigJson(
						m_runtimeDeepSeekApiKey,
						m_runtimeDeepSeekBaseUrl,
						m_runtimeDeepSeekDefaultModel) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.summary", [](const protocol::RequestFrame& request) {
			const auto& events = EventCatalogNames();
			const std::size_t lifecycle = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [](const std::string& item) {
				return item == "gateway.tick" || item == "gateway.health" || item == "gateway.shutdown";
				}));
			const std::size_t updates = events.size() - lifecycle;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"total\":" + std::to_string(events.size()) + ",\"lifecycle\":" + std::to_string(lifecycle) + ",\"updates\":" + std::to_string(updates) + "}",
				.error = std::nullopt,
			};
			});



		m_dispatcher.Register("gateway.events.search", [](const protocol::RequestFrame& request) {
			const std::string term = ExtractStringParam(request.paramsJson, "term");
			const auto& events = EventCatalogNames();
			std::string eventsJson = "[";
			std::size_t count = 0;
			for (std::size_t i = 0; i < events.size(); ++i) {
				if (!term.empty() && events[i].find(term) == std::string::npos) {
					continue;
				}
				if (count > 0) {
					eventsJson += ",";
				}
				eventsJson += "\"" + EscapeJson(events[i]) + "\"";
				++count;
			}
			eventsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"term\":\"" + EscapeJson(term.empty() ? "*" : term) + "\",\"events\":" + eventsJson + ",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.last", [](const protocol::RequestFrame& request) {
			const auto& events = EventCatalogNames();
			const std::string last = events.empty() ? "none" : events.back();
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(last) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.categories", [this](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			std::vector<std::string> categories;
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (std::find(categories.begin(), categories.end(), tools[i].category) == categories.end()) {
					categories.push_back(tools[i].category);
				}
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"categories\":" + SerializeStringArray(categories) + ",\"count\":" + std::to_string(categories.size()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.list", [](const protocol::RequestFrame& request) {
			const auto& events = EventCatalogNames();
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"events\":" + SerializeStringArray(events) + ",\"count\":" + std::to_string(events.size()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const auto agents = m_agentRegistry.List();
			const bool exists = std::any_of(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return requestedId.empty() || agent.id == requestedId;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agentId\":\"" + EscapeJson(requestedId.empty() ? "*" : requestedId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.getSection", [this](const protocol::RequestFrame& request) {
			const std::string section = ExtractStringParam(request.paramsJson, "section");
			const std::string resolved = section.empty() ? "gateway" : section;
			std::string sectionJson = "{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) + "\",\"port\":" + std::to_string(m_runtimeGatewayPort) + "}";
			if (resolved == "agent") {
				sectionJson = "{\"model\":\"" + EscapeJson(m_runtimeAgentModel) + "\",\"streaming\":" + std::string(m_runtimeAgentStreaming ? "true" : "false") + "}";
			}
			else if (resolved == "deepseek") {
				sectionJson = BuildDeepSeekConfigJson(
					m_runtimeDeepSeekApiKey,
					m_runtimeDeepSeekBaseUrl,
					m_runtimeDeepSeekDefaultModel);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"section\":\"" + EscapeJson(resolved) + "\",\"config\":" + sectionJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.get", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const auto tools = m_toolRegistry.List();
			ToolCatalogEntry selected{};
			if (!tools.empty()) {
				selected = tools.front();
			}
			for (const auto& tool : tools) {
				if (!requestedTool.empty() && tool.id != requestedTool) {
					continue;
				}
				selected = tool;
				break;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":" + SerializeTool(selected) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.count", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto agents = m_agentRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(agents.begin(), agents.end(), [&](const AgentEntry& agent) {
				return !activeFilter.has_value() || agent.active == activeFilter.value();
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
					",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
					",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.get", [](const protocol::RequestFrame& request) {
			const std::string modelId = ExtractStringParam(request.paramsJson, "modelId");
			const std::string resolvedId = NormalizeModelId(modelId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			   .payloadJson = "{\"model\":" + BuildModelJson(resolvedId) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const auto sessions = m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessionId\":\"" + EscapeJson(requestedId.empty() ? "*" : requestedId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.getKey", [this](const protocol::RequestFrame& request) {
			const std::string key = ExtractStringParam(request.paramsJson, "key");
			std::string value;
			if (key == "gateway.bind") {
				value = m_runtimeGatewayBind;
			}
			else if (key == "gateway.port") {
				value = std::to_string(m_runtimeGatewayPort);
			}
			else if (key == "agent.model") {
				value = m_runtimeAgentModel;
			}
			else if (key == "agent.streaming") {
				value = m_runtimeAgentStreaming ? "true" : "false";
			}
			else if (key == "deepseek.apiKey") {
				value = MaskSecret(m_runtimeDeepSeekApiKey);
			}
			else if (key == "deepseek.baseUrl") {
				value = m_runtimeDeepSeekBaseUrl;
			}
			else if (key == "deepseek.defaultModel") {
				value = m_runtimeDeepSeekDefaultModel;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"key\":\"" + EscapeJson(key.empty() ? "gateway.bind" : key) +
					"\",\"value\":\"" + EscapeJson(value.empty() ? m_runtimeGatewayBind : value) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.count", [this](const protocol::RequestFrame& request) {
			const std::string scope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> active = ExtractBooleanParam(request.paramsJson, "active");
			const auto sessions = m_sessionRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				if (!scope.empty() && session.scope != scope) {
					return false;
				}
				if (active.has_value() && session.active != active.value()) {
					return false;
				}
				return true;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"scope\":\"" + EscapeJson(scope.empty() ? "*" : scope) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.transport.endpoint.exists", [this](const protocol::RequestFrame& request) {
			const std::string endpoint = ExtractStringParam(request.paramsJson, "endpoint");
			const std::string current = m_transport.Endpoint();
			const bool exists = endpoint.empty() || endpoint == current;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"endpoint\":\"" + EscapeJson(endpoint.empty() ? current : endpoint) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.activate", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const auto sessions = m_sessionRegistry.List();
			const bool exists = std::any_of(sessions.begin(), sessions.end(), [&](const SessionEntry& session) {
				return requestedId.empty() || session.id == requestedId;
				});
			const SessionEntry activated = m_sessionRegistry.Patch(requestedId, std::nullopt, true);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(activated) +
					",\"activated\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});


		m_dispatcher.Register("gateway.agents.files.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			const AgentFileExistsResult result = m_agentRegistry.ExistsFile(requestedId, requestedPath);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"path\":\"" + EscapeJson(result.path) +
					"\",\"exists\":" + std::string(result.exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			if (IsUnsafeAgentFilePath(requestedPath)) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJson(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.files.delete::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != m_mutationPayloadByIdempotency.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson = dedupeIt->second,
						.error = std::nullopt,
					};
				}
			}

			const AgentFileDeleteResult result = m_agentRegistry.DeleteFile(requestedId, requestedPath);
			const std::string payload =
				"{\"file\":" + SerializeAgentFileContent(result.file) +
				",\"deleted\":" + std::string(result.deleted ? "true" : "false") + "}";

			if (!idempotencyKey.empty()) {
				m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			  .payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.set", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			if (IsUnsafeAgentFilePath(requestedPath)) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJson(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			const std::string content = ExtractStringParam(request.paramsJson, "content");
			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.files.set::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != m_mutationPayloadByIdempotency.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson = dedupeIt->second,
						.error = std::nullopt,
					};
				}
			}

			const AgentFileContentEntry file = m_agentRegistry.SetFile(requestedId, requestedPath, content);
			const std::string payload = "{\"file\":" + SerializeAgentFileContent(file) + ",\"saved\":true}";

			if (!idempotencyKey.empty()) {
				m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			 .payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.get", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedPath = ExtractStringParam(request.paramsJson, "path");
			if (IsUnsafeAgentFilePath(requestedPath)) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = protocol::ErrorShape{
						.code = "invalid_path",
						.message = "Agent file path is not allowed.",
						.detailsJson = "{\"path\":\"" + EscapeJson(requestedPath) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}
			const AgentFileContentEntry file = m_agentRegistry.GetFile(requestedId, requestedPath);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"file\":" + SerializeAgentFileContent(file) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.files.list", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const auto files = m_agentRegistry.ListFiles(requestedId);
			std::string filesJson = "[";
			for (std::size_t i = 0; i < files.size(); ++i) {
				if (i > 0) {
					filesJson += ",";
				}

				filesJson += SerializeAgentFile(files[i]);
			}

			filesJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"files\":" + filesJson + ",\"count\":" + std::to_string(files.size()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.list", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			 .payloadJson = "{\"models\":[" +
					BuildModelJson(kDefaultModelId) + "," +
					BuildModelJson(kReasonerModelId) + "," +
					BuildModelJson(kDeepSeekChatModelId) + "," +
					BuildModelJson(kDeepSeekReasonerModelId) + "]}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.call.execute", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const std::optional<std::string> argsJson = ExtractObjectParam(request.paramsJson, "args");
			const bool argsProvided = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;

			// Emit telemetry for tool invocation attempt
			{
				const std::string invokePayload =
					"{\"tool\":" + JsonString(requestedTool) +
					",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") + "}";
				EmitTelemetryEvent("gateway.tool.invoke", invokePayload);
			}

			const ToolExecuteResult execution = m_toolRegistry.Execute(requestedTool, argsJson);

			// Emit telemetry for tool execution result
			{
				const std::string resultPayload =
					"{\"tool\":" + JsonString(execution.tool) +
					",\"executed\":" + std::string(execution.executed ? "true" : "false") +
					",\"status\":" + JsonString(execution.status) +
					",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") + "}";
				EmitTelemetryEvent("gateway.tool.complete", resultPayload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(execution.tool) +
					"\",\"executed\":" + std::string(execution.executed ? "true" : "false") +
					",\"status\":\"" + EscapeJson(execution.status) +
					"\",\"output\":\"" + EscapeJson(execution.output) +
					"\",\"argsProvided\":" + std::string(argsProvided ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});


		m_dispatcher.Register("gateway.agents.update", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedName = ExtractStringParam(request.paramsJson, "name");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.update::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != m_mutationPayloadByIdempotency.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson = dedupeIt->second,
						.error = std::nullopt,
					};
				}
			}

			const AgentEntry updated = m_agentRegistry.Update(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);
			const std::string payload = "{\"agent\":" + SerializeAgent(updated) + ",\"updated\":true}";

			if (!idempotencyKey.empty()) {
				m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			  .payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.delete::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != m_mutationPayloadByIdempotency.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson = dedupeIt->second,
						.error = std::nullopt,
					};
				}
			}

			AgentEntry removedAgent;
			const bool deleted = m_agentRegistry.Delete(requestedId, removedAgent);
			const std::size_t remaining = m_agentRegistry.List().size();
			const std::string payload =
				"{\"agent\":" + SerializeAgent(removedAgent) +
				",\"deleted\":" + std::string(deleted ? "true" : "false") +
				",\"remaining\":" + std::to_string(remaining) + "}";

			if (!idempotencyKey.empty()) {
				m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			   .payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.create", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedName = ExtractStringParam(request.paramsJson, "name");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");
			const std::string mutationDedupeKey = "gateway.agents.create::" + idempotencyKey;
			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_mutationPayloadByIdempotency.find(mutationDedupeKey);
				if (dedupeIt != m_mutationPayloadByIdempotency.end()) {
					return protocol::ResponseFrame{
						.id = request.id,
						.ok = true,
						.payloadJson = dedupeIt->second,
						.error = std::nullopt,
					};
				}
			}

			const AgentEntry created = m_agentRegistry.Create(
				requestedId,
				requestedName.empty() ? std::nullopt : std::optional<std::string>(requestedName),
				requestedActive);
			const std::string payload = "{\"agent\":" + SerializeAgent(created) + ",\"created\":true}";

			if (!idempotencyKey.empty()) {
				m_mutationPayloadByIdempotency.insert_or_assign(mutationDedupeKey, payload);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
			  .payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.patch", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");
			const SessionEntry patched = m_sessionRegistry.Patch(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(patched) + ",\"patched\":true}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.preview", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const SessionEntry session = m_sessionRegistry.Resolve(requestedId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(session) +
					",\"title\":\"Session " + EscapeJson(session.id) +
					"\",\"hasMessages\":true,\"unread\":0}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.compact", [this](const protocol::RequestFrame& request) {
			const bool dryRun = ExtractBooleanParam(request.paramsJson, "dryRun").value_or(false);
			const std::size_t compacted = dryRun ? m_sessionRegistry.CountCompactCandidates() : m_sessionRegistry.CompactInactive();
			const std::size_t remaining = m_sessionRegistry.List().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"compacted\":" + std::to_string(compacted) +
					",\"remaining\":" + std::to_string(remaining) +
					",\"dryRun\":" + std::string(dryRun ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.usage", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const SessionEntry session = m_sessionRegistry.Resolve(requestedId);

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessionId\":\"" + EscapeJson(session.id) +
					"\",\"messages\":42,\"tokens\":{\"input\":1024,\"output\":512,\"total\":1536},\"lastActiveMs\":1735689600200}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.delete", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			SessionEntry removedSession;
			const bool deleted = m_sessionRegistry.Delete(requestedId, removedSession);
			const std::size_t remaining = m_sessionRegistry.List().size();

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(removedSession) +
					",\"deleted\":" + std::string(deleted ? "true" : "false") +
					",\"remaining\":" + std::to_string(remaining) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.features.list", [this](const protocol::RequestFrame& request) {
			const std::string methodsJson = SerializeStringArray(m_dispatcher.RegisteredMethods());
			const std::string eventsJson = SerializeStringArray(EventCatalogNames());

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"methods\":" + methodsJson + ",\"events\":" + eventsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.list", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto agents = m_agentRegistry.List();
			std::string payload = "{\"agents\":[";
			bool first = true;
			std::string activeAgentId = "none";
			std::size_t count = 0;
			for (std::size_t i = 0; i < agents.size(); ++i) {
				if (activeFilter.has_value() && agents[i].active != activeFilter.value()) {
					continue;
				}

				if (!first) {
					payload += ",";
				}

				payload += SerializeAgent(agents[i]);
				if (activeAgentId == "none" && agents[i].active) {
					activeAgentId = agents[i].id;
				}

				first = false;
				++count;
			}

			payload += "],\"count\":" + std::to_string(count) + ",\"activeAgentId\":\"" + EscapeJson(activeAgentId) + "\"}";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.run", [this](const protocol::RequestFrame& request) {
			const std::string requestedAgentId = ExtractStringParam(request.paramsJson, "agentId");
			const std::string requestedSessionId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string message = ExtractStringParam(request.paramsJson, "message");
			const std::string idempotencyKey = ExtractStringParam(request.paramsJson, "idempotencyKey");

			if (!idempotencyKey.empty()) {
				const auto dedupeIt = m_agentRunByIdempotency.find(idempotencyKey);
				if (dedupeIt != m_agentRunByIdempotency.end()) {
					const auto runIt = m_agentRuns.find(dedupeIt->second);
					if (runIt != m_agentRuns.end()) {
						const auto& run = runIt->second;
						const std::string completedMsJson = run.completedAtMs.has_value()
							? std::to_string(run.completedAtMs.value())
							: "null";
						return protocol::ResponseFrame{
							.id = request.id,
							.ok = true,
							.payloadJson =
								"{\"runId\":\"" + EscapeJson(run.runId) +
								"\",\"status\":\"" + EscapeJson(run.status) +
								"\",\"agentId\":\"" + EscapeJson(run.agentId) +
								"\",\"sessionId\":\"" + EscapeJson(run.sessionId) +
								"\",\"summary\":\"" + EscapeJson(run.summary) +
								"\",\"deduped\":true,\"startedAtMs\":" +
								std::to_string(run.startedAtMs) +
								",\"completedAtMs\":" + completedMsJson + "}",
							.error = std::nullopt,
						};
					}
				}
			}

			const AgentEntry agent = m_agentRegistry.Get(requestedAgentId);
			const SessionEntry session = m_sessionRegistry.Resolve(requestedSessionId);
			const std::uint64_t startedAtMs = CurrentEpochMs();
			const std::string runId =
				"run-" + std::to_string(startedAtMs) + "-" + agent.id;

			std::string chatParamsJson =
				"{\"sessionKey\":\"" + EscapeJson(session.id) +
				"\",\"message\":\"" + EscapeJson(message) + "\"";
			if (!idempotencyKey.empty()) {
				chatParamsJson +=
					",\"idempotencyKey\":\"agents-run::" +
					EscapeJson(idempotencyKey) + "\"";
			}
			chatParamsJson += "}";

			const protocol::RequestFrame chatRequest{
				.id = runId,
				.method = "chat.send",
				.paramsJson = chatParamsJson,
			};
			const protocol::ResponseFrame chatResponse =
				RouteRequest(chatRequest);
			if (!chatResponse.ok || !chatResponse.payloadJson.has_value()) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = chatResponse.error.has_value()
						? chatResponse.error
						: std::optional<protocol::ErrorShape>(
							protocol::ErrorShape{
								.code = "chat_dispatch_failed",
								.message = "Unable to start chat runtime flow.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							}),
				};
			}

			std::string chatRunId;
			json::FindStringField(
				chatResponse.payloadJson.value(),
				"runId",
				chatRunId);
			if (chatRunId.empty()) {
				chatRunId = runId;
			}

			std::string backendErrorCode;
			json::FindStringField(
				chatResponse.payloadJson.value(),
				"backendErrorCode",
				backendErrorCode);

			AgentRunState run{
				.runId = chatRunId,
				.agentId = agent.id,
				.sessionId = session.id,
				.message = message,
				.status = "queued",
				.summary = backendErrorCode.empty() ? "queued" : backendErrorCode,
				.startedAtMs = startedAtMs,
				.completedAtMs = std::nullopt,
			};

			m_agentRuns.insert_or_assign(chatRunId, run);
			if (!idempotencyKey.empty()) {
				m_agentRunByIdempotency.insert_or_assign(idempotencyKey, chatRunId);
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
				   "{\"runId\":\"" + EscapeJson(run.runId) +
					"\",\"status\":\"" + EscapeJson(run.status) +
					"\",\"agentId\":\"" + EscapeJson(run.agentId) +
					"\",\"sessionId\":\"" + EscapeJson(run.sessionId) +
					"\",\"summary\":\"" + EscapeJson(run.summary) +
					"\",\"deduped\":false,\"startedAtMs\":" +
					std::to_string(run.startedAtMs) +
				 ",\"completedAtMs\":null}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.wait", [this](const protocol::RequestFrame& request) {
			const std::string runId = ExtractStringParam(request.paramsJson, "runId");
			const auto runIt = m_agentRuns.find(runId);
			if (runIt == m_agentRuns.end()) {
				return protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = protocol::ErrorShape{
						.code = "run_not_found",
						.message = "Agent run was not found.",
						.detailsJson = "{\"runId\":\"" + EscapeJson(runId) + "\"}",
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			auto& run = runIt->second;
			const auto chatRunIt = m_chatRunsById.find(runId);
			if (chatRunIt != m_chatRunsById.end()) {
				const auto& chatRun = chatRunIt->second;
				if (chatRun.failed) {
					run.status = "failed";
					run.summary = chatRun.errorMessage.empty()
						? "chat_runtime_error"
						: chatRun.errorMessage;
				}
				else if (chatRun.active) {
					run.status = "running";
					run.summary = "running";
				}
				else {
					run.status = "completed";
					run.summary = "completed";
				}

				if (!chatRun.active && !run.completedAtMs.has_value()) {
					run.completedAtMs = CurrentEpochMs();
				}
			}
			else {
				const auto taskDeltasIt = m_taskDeltasByRunId.find(runId);
				if (taskDeltasIt != m_taskDeltasByRunId.end() &&
					!taskDeltasIt->second.empty()) {
					auto orderedTaskDeltas = taskDeltasIt->second;
					std::sort(
						orderedTaskDeltas.begin(),
						orderedTaskDeltas.end(),
						[](const ChatRuntimeResult::TaskDeltaEntry& left,
							const ChatRuntimeResult::TaskDeltaEntry& right) {
								return left.index < right.index;
						});

					for (auto it = orderedTaskDeltas.rbegin();
						it != orderedTaskDeltas.rend();
						++it) {
						if (it->phase != "final") {
							continue;
						}

						run.status = it->status.empty() ? "completed" : it->status;
						run.summary = (run.status == "failed")
							? (it->errorCode.empty() ? "failed" : it->errorCode)
							: "completed";
						if (!run.completedAtMs.has_value()) {
							run.completedAtMs = it->completedAtMs > 0
								? it->completedAtMs
								: CurrentEpochMs();
						}
						break;
					}
				}

				if (m_chatTerminalDeliveredRunIds.find(runId) !=
					m_chatTerminalDeliveredRunIds.end()) {
					if (run.status == "queued" || run.status == "running") {
						run.status = "completed";
						run.summary = "completed";
					}

					if (!run.completedAtMs.has_value()) {
						run.completedAtMs = CurrentEpochMs();
					}
				}
			}

			const std::string completedMsJson = run.completedAtMs.has_value()
				? std::to_string(run.completedAtMs.value())
				: "null";
			const std::string terminal = run.completedAtMs.has_value() ||
				run.status == "completed" ||
				run.status == "failed" ||
				run.status == "aborted"
				? "true"
				: "false";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson =
					"{\"runId\":\"" + EscapeJson(run.runId) +
					"\",\"status\":\"" + EscapeJson(run.status) +
					"\",\"summary\":\"" + EscapeJson(run.summary) +
					"\",\"terminal\":" + terminal +
					",\"agentId\":\"" + EscapeJson(run.agentId) +
					"\",\"sessionId\":\"" + EscapeJson(run.sessionId) +
					"\",\"startedAtMs\":" + std::to_string(run.startedAtMs) +
					",\"completedAtMs\":" + completedMsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.accounts", [this](const protocol::RequestFrame& request) {
			const std::string channelFilter = ExtractStringParam(request.paramsJson, "channel");
			const auto accounts = m_channelRegistry.ListAccounts();
			std::string accountsJson = "[";
			bool first = true;
			for (std::size_t i = 0; i < accounts.size(); ++i) {
				if (!channelFilter.empty() && accounts[i].channel != channelFilter) {
					continue;
				}

				if (!first) {
					accountsJson += ",";
				}

				accountsJson += SerializeChannelAccount(accounts[i]);
				first = false;
			}

			accountsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"accounts\":" + accountsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.get", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			ChannelStatusEntry selected{};
			bool found = false;
			for (const auto& status : statuses) {
				if (!channel.empty() && status.id != channel) {
					continue;
				}
				selected = status;
				found = true;
				break;
			}
			if (!found) {
				if (!statuses.empty()) {
					selected = statuses.front();
				}
				else {
					selected = ChannelStatusEntry{ .id = channel.empty() ? "unknown" : channel, .label = "Unknown", .connected = false, .accountCount = 0 };
				}
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":" + SerializeChannelStatus(selected) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.exists", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			const bool exists = std::any_of(statuses.begin(), statuses.end(), [&](const ChannelStatusEntry& status) {
				return channel.empty() || status.id == channel;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.status.count", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const auto statuses = m_channelRegistry.ListStatus();
			const std::size_t count = static_cast<std::size_t>(std::count_if(statuses.begin(), statuses.end(), [&](const ChannelStatusEntry& status) {
				return channel.empty() || status.id == channel;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"channel\":\"" + EscapeJson(channel.empty() ? "*" : channel) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.channels.route.resolve", [this](const protocol::RequestFrame& request) {
			const std::string channel = ExtractStringParam(request.paramsJson, "channel");
			const std::string account = ExtractStringParam(request.paramsJson, "accountId");
			const std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
			const ChannelRouteEntry route = m_channelRegistry.ResolveRoute(channel, account);
			const SessionEntry session = m_sessionRegistry.Resolve(sessionId.empty() ? route.sessionId : sessionId);
			const AgentEntry agent = m_agentRegistry.Get(agentId.empty() ? route.agentId : agentId);
			const ChannelRouteEntry resolvedRoute{
				.channel = route.channel,
				.accountId = route.accountId,
				.agentId = agent.id,
				.sessionId = session.id,
			};

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"route\":" + SerializeChannelRoute(resolvedRoute) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.get", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const AgentEntry agent = m_agentRegistry.Get(requestedId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(agent) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.catalog", [this](const protocol::RequestFrame& request) {
			const auto tools = m_toolRegistry.List();
			std::string toolsJson = "[";
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (i > 0) {
					toolsJson += ",";
				}

				toolsJson += SerializeTool(tools[i]);
			}

			toolsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tools\":" + toolsJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.call.preview", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const ToolPreviewResult preview = m_toolRegistry.Preview(requestedTool);
			const bool argsProvided = request.paramsJson.has_value() &&
				request.paramsJson.value().find("\"args\"") != std::string::npos;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(preview.tool) + "\",\"allowed\":" +
					std::string(preview.allowed ? "true" : "false") + ",\"reason\":\"" +
					EscapeJson(preview.reason) + "\",\"argsProvided\":" +
					std::string(argsProvided ? "true" : "false") +
				   ",\"policy\":\"dynamic_runtime_preview_v1\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.agents.activate", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "agentId");
			const AgentEntry agent = m_agentRegistry.Activate(requestedId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"agent\":" + SerializeAgent(agent) + ",\"event\":\"gateway.agent.update\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.get", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
	 .payloadJson = "{\"gateway\":{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) +
			"\",\"port\":" + std::to_string(m_runtimeGatewayPort) +
			"},\"agent\":{\"model\":\"" + EscapeJson(m_runtimeAgentModel) +
			"\",\"streaming\":" + std::string(m_runtimeAgentStreaming ? "true" : "false") +
			"},\"emailFallback\":{\"preflightEnabled\":" +
			std::string(m_runtimeEmailPreflightEnabled ? "true" : "false") +
			",\"policyProfilesEnabled\":" +
			std::string(m_runtimeEmailPolicyProfilesEnabled ? "true" : "false") +
			",\"policyProfilesEnforce\":" +
			std::string(m_runtimeEmailPolicyProfilesEnforce ? "true" : "false") +
			"},\"deepseek\":" + BuildDeepSeekConfigJson(
				m_runtimeDeepSeekApiKey,
				m_runtimeDeepSeekBaseUrl,
				m_runtimeDeepSeekDefaultModel) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.set", [this](const protocol::RequestFrame& request) {
			const std::string bind = ExtractStringParam(request.paramsJson, "bind");
			const std::optional<std::size_t> port = ExtractNumericParam(request.paramsJson, "port");
			const std::string model = ExtractStringParam(request.paramsJson, "model");
			const std::optional<bool> streaming = ExtractBooleanParam(request.paramsJson, "streaming");
			const std::string deepSeekApiKey = ExtractStringParam(request.paramsJson, "deepseekApiKey");
			const std::string deepSeekBaseUrl = ExtractStringParam(request.paramsJson, "deepseekBaseUrl");

			if (!bind.empty()) {
				m_runtimeGatewayBind = bind;
			}

			if (port.has_value() && port.value() > 0 && port.value() <= 65535) {
				m_runtimeGatewayPort = static_cast<std::uint16_t>(port.value());
			}

			if (!model.empty()) {
				m_runtimeAgentModel = NormalizeModelId(model);
			}

			if (streaming.has_value()) {
				m_runtimeAgentStreaming = streaming.value();
			}

			if (!deepSeekApiKey.empty()) {
				m_runtimeDeepSeekApiKey = deepSeekApiKey;
			}

			if (!deepSeekBaseUrl.empty()) {
				m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
			}

			if (model.empty() && !deepSeekApiKey.empty() &&
				(m_runtimeAgentModel == kDefaultModelId ||
					m_runtimeAgentModel == kReasonerModelId)) {
				m_runtimeAgentModel = m_runtimeDeepSeekDefaultModel;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"gateway\":{\"bind\":\"" + EscapeJson(m_runtimeGatewayBind) +
					"\",\"port\":" + std::to_string(m_runtimeGatewayPort) +
					"},\"agent\":{\"model\":\"" + EscapeJson(m_runtimeAgentModel) +
					"\",\"streaming\":" + std::string(m_runtimeAgentStreaming ? "true" : "false") +
				  "},\"deepseek\":" + BuildDeepSeekConfigJson(
						m_runtimeDeepSeekApiKey,
						m_runtimeDeepSeekBaseUrl,
						m_runtimeDeepSeekDefaultModel) +
					",\"updated\":true}",
				.error = std::nullopt,
			};
			});


		m_dispatcher.Register("gateway.logs.tail", [](const protocol::RequestFrame& request) {
			const std::vector<std::string> seededEntries = {
				"{\"ts\":1735689600000,\"level\":\"info\",\"source\":\"gateway\",\"message\":\"Gateway host started\"}",
				"{\"ts\":1735689600100,\"level\":\"info\",\"source\":\"transport\",\"message\":\"WebSocket listener active\"}",
				"{\"ts\":1735689600200,\"level\":\"debug\",\"source\":\"dispatcher\",\"message\":\"Method handlers registered\"}",
			};

			const std::size_t requestedLimit = ExtractNumericParam(request.paramsJson, "limit").value_or(50);
			const std::size_t cappedLimit = std::max<std::size_t>(1, std::min<std::size_t>(requestedLimit, 200));
			const std::size_t emitCount = std::min<std::size_t>(cappedLimit, seededEntries.size());
			const std::size_t begin = seededEntries.size() - emitCount;

			std::string entriesJson = "[";
			for (std::size_t i = begin; i < seededEntries.size(); ++i) {
				if (i > begin) {
					entriesJson += ",";
				}

				entriesJson += seededEntries[i];
			}

			entriesJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"entries\":" + entriesJson + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.resolve", [this](const protocol::RequestFrame& request) {
			const std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");

			const SessionEntry resolved = m_sessionRegistry.Resolve(sessionId);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(resolved) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.create", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");

			const SessionEntry created = m_sessionRegistry.Create(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(created) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.sessions.reset", [this](const protocol::RequestFrame& request) {
			const std::string requestedId = ExtractStringParam(request.paramsJson, "sessionId");
			const std::string requestedScope = ExtractStringParam(request.paramsJson, "scope");
			const std::optional<bool> requestedActive = ExtractBooleanParam(request.paramsJson, "active");

			const SessionEntry reset = m_sessionRegistry.Reset(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"session\":" + SerializeSession(reset) + ",\"event\":\"gateway.session.reset\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.health", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"status\":\"ok\",\"running\":" + std::string(IsRunning() ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		RegisterSecurityOpsHandlers();

		RegisterRuntimeHandlers();

		RegisterTransportHandlers();

		m_dispatcher.Register("gateway.session.list", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const std::string scopeFilter = ExtractStringParam(request.paramsJson, "scope");
			const auto sessions = m_sessionRegistry.List();
			std::string sessionArray = "[";
			bool first = true;
			std::size_t count = 0;
			std::string activeSessionId = "none";
			for (std::size_t i = 0; i < sessions.size(); ++i) {
				if (activeFilter.has_value() && sessions[i].active != activeFilter.value()) {
					continue;
				}

				if (!scopeFilter.empty() && sessions[i].scope != scopeFilter) {
					continue;
				}

				if (!first) {
					sessionArray += ",";
				}

				sessionArray += SerializeSession(sessions[i]);
				if (activeSessionId == "none" && sessions[i].active) {
					activeSessionId = sessions[i].id;
				}

				first = false;
				++count;
			}

			sessionArray += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"sessions\":" + sessionArray + ",\"count\":" + std::to_string(count) +
					",\"activeSessionId\":\"" + EscapeJson(activeSessionId) + "\"}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.catalog", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"events\":" + SerializeStringArray(EventCatalogNames()) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.exists", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			const bool exists = std::any_of(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(eventName.empty() ? "*" : eventName) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.count", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			const std::size_t count = static_cast<std::size_t>(std::count_if(events.begin(), events.end(), [&](const std::string& item) {
				return eventName.empty() || item == eventName;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(eventName.empty() ? "*" : eventName) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.exists", [this](const protocol::RequestFrame& request) {
			const std::string requestedTool = ExtractStringParam(request.paramsJson, "tool");
			const auto tools = m_toolRegistry.List();
			const bool exists = std::any_of(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return requestedTool.empty() || tool.id == requestedTool;
				});

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tool\":\"" + EscapeJson(requestedTool.empty() ? "*" : requestedTool) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.count", [this](const protocol::RequestFrame& request) {
			const std::optional<bool> activeFilter = ExtractBooleanParam(request.paramsJson, "active");
			const auto tools = m_toolRegistry.List();
			const std::size_t count = static_cast<std::size_t>(std::count_if(tools.begin(), tools.end(), [&](const ToolCatalogEntry& tool) {
				return !activeFilter.has_value() || tool.enabled == activeFilter.value();
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"active\":" + std::string(activeFilter.value_or(false) ? "true" : "false") +
					",\"activeFilterApplied\":" + std::string(activeFilter.has_value() ? "true" : "false") +
					",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.exists", [](const protocol::RequestFrame& request) {
			const std::string modelId = ExtractStringParam(request.paramsJson, "modelId");
			const bool exists = modelId.empty() || modelId == "default" || modelId == "reasoner";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"modelId\":\"" + EscapeJson(modelId.empty() ? "*" : modelId) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.exists", [this](const protocol::RequestFrame& request) {
			const std::string key = ExtractStringParam(request.paramsJson, "key");
			const bool exists = key.empty() ||
				key == "gateway.bind" ||
				key == "gateway.port" ||
				key == "agent.model" ||
				key == "agent.streaming" ||
				key == "embeddings.enabled" ||
				key == "embeddings.provider" ||
				key == "embeddings.model_path" ||
				key == "embeddings.tokenizer_path" ||
				key == "embeddings.dimension" ||
				key == "embeddings.max_sequence_length" ||
				key == "embeddings.normalize" ||
				key == "embeddings.intra_threads" ||
				key == "embeddings.inter_threads" ||
				key == "embeddings.execution_mode";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"key\":\"" + EscapeJson(key.empty() ? "*" : key) +
					"\",\"exists\":" + std::string(exists ? "true" : "false") + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.health.details", [this](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"status\":\"ok\",\"running\":" + std::string(IsRunning() ? "true" : "false") +
					",\"transport\":{\"running\":" + std::string(m_transport.IsRunning() ? "true" : "false") +
					",\"endpoint\":\"" + EscapeJson(m_transport.Endpoint()) + "\",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.logs.count", [](const protocol::RequestFrame& request) {
			const std::string level = ExtractStringParam(request.paramsJson, "level");
			const std::vector<std::string> levels = { "info", "info", "debug" };
			const std::size_t count = static_cast<std::size_t>(std::count_if(levels.begin(), levels.end(), [&](const std::string& item) {
				return level.empty() || item == level;
				}));

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"level\":\"" + EscapeJson(level.empty() ? "*" : level) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.config.count", [](const protocol::RequestFrame& request) {
			const std::string section = ExtractStringParam(request.paramsJson, "section");
			const std::size_t count = section == "gateway" || section == "agent"
				? 2
				: 4;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"section\":\"" + EscapeJson(section.empty() ? "*" : section) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.count", [](const protocol::RequestFrame& request) {
			const std::string provider = ExtractStringParam(request.paramsJson, "provider");
			const std::size_t count = provider.empty() || provider == "seed" ? 2 : 0;

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"provider\":\"" + EscapeJson(provider.empty() ? "*" : provider) +
					"\",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.events.get", [](const protocol::RequestFrame& request) {
			const std::string eventName = ExtractStringParam(request.paramsJson, "event");
			const auto& events = EventCatalogNames();
			std::string selected = events.empty() ? "unknown" : events.front();
			for (const auto& item : events) {
				if (!eventName.empty() && item != eventName) {
					continue;
				}
				selected = item;
				break;
			}

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"event\":\"" + EscapeJson(selected) + "\"}",
				.error = std::nullopt,
			};
			});


		m_dispatcher.Register("gateway.logs.levels", [](const protocol::RequestFrame& request) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"levels\":[\"info\",\"debug\"],\"count\":2}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.tools.list", [this](const protocol::RequestFrame& request) {
			const std::string category = ExtractStringParam(request.paramsJson, "category");
			const auto tools = m_toolRegistry.List();
			std::string toolsJson = "[";
			std::size_t count = 0;
			for (std::size_t i = 0; i < tools.size(); ++i) {
				if (!category.empty() && tools[i].category != category) {
					continue;
				}
				if (count > 0) {
					toolsJson += ",";
				}
				toolsJson += SerializeTool(tools[i]);
				++count;
			}
			toolsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"tools\":" + toolsJson + ",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});

		m_dispatcher.Register("gateway.models.listByProvider", [](const protocol::RequestFrame& request) {
			const std::string provider = ExtractStringParam(request.paramsJson, "provider");
			const bool includeAll = provider.empty();
			const bool includeSeed = includeAll || provider == "seed";
			const bool includeDeepSeek = includeAll || provider == "deepseek";

			std::string modelsJson = "[";
			std::size_t count = 0;
			if (includeSeed) {
				modelsJson += BuildModelJson(kDefaultModelId);
				modelsJson += "," + BuildModelJson(kReasonerModelId);
				count += 2;
			}

			if (includeDeepSeek) {
				if (count > 0) {
					modelsJson += ",";
				}

				modelsJson += BuildModelJson(kDeepSeekChatModelId);
				modelsJson += "," + BuildModelJson(kDeepSeekReasonerModelId);
				count += 2;
			}

			modelsJson += "]";

			return protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = "{\"provider\":\"" + EscapeJson(provider.empty() ? "*" : provider) + "\",\"models\":" + modelsJson + ",\"count\":" + std::to_string(count) + "}",
				.error = std::nullopt,
			};
			});
	}

} // namespace blazeclaw::gateway
