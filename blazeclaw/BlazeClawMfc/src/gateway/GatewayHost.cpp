#include "pch.h"
#include "GatewayHost.h"
#include "../core/diagnostics/DiagnosticsSnapshot.h"

#include "GatewayJsonUtils.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayHostHandlersToolsShared.h"
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
#include "GatewayMethodSurfaceAudit.h"
#include "GatewayHttpAuthService.h"
#include "Telemetry.h"

#include <algorithm>
#include <iterator>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	std::vector<std::string> GatewayHost::ListReservedChatSlashCommandNames() {
		return std::vector<std::string>{
			"help",
				"skill",
				"agent",
				"agents",
				"session",
				"sessions",
				"channel",
				"channels",
				"tool",
				"tools",
		};
	}

	namespace {
		std::string EscapeJson(const std::string& value) {
			return EscapeJsonString(value);
		}

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

		std::string ToNarrow(const std::wstring& value) {
			std::string result;
			result.reserve(value.size());

			for (const wchar_t ch : value) {
				result.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return result;
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
			std::vector<std::string> rows;
			rows.reserve(matches.size());
			for (const auto& text : matches) {
				rows.push_back(JsonObject({ {"text", JsonString(text)} }));
			}

			return JsonObject({
				{"sessionKey", JsonString(sessionKey)},
				{"matches", JsonArray(rows)},
				{"count", JsonNumber(static_cast<std::uint64_t>(matches.size()))},
				});
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

	bool GatewayHost::BootstrapCreateRuntimeState(
		const blazeclaw::config::GatewayConfig& config) {
		if (!m_dispatchInitialized) {
			RegisterDefaultHandlers();
			m_dispatchInitialized = true;
		}

		if (m_initialized) {
			return true;
		}

		return CreateRuntimeState(config);
	}

	bool GatewayHost::BootstrapStartRuntimeServices() {
		if (m_initialized) {
			return true;
		}

		return StartRuntimeServices();
	}

	bool GatewayHost::BootstrapAttachTransportHandlers() {
		if (m_initialized) {
			return true;
		}

		return AttachTransportRuntime();
	}

	bool GatewayHost::BootstrapStartRuntimeSubscriptions() {
		if (m_initialized) {
			return true;
		}

		return StartRuntimeSubscriptions();
	}

	bool GatewayHost::BootstrapFinalizeRuntimeInitialization() {
		if (m_initialized) {
			return true;
		}

		return FinalizeRuntimeInitialization();
	}

	bool GatewayHost::StartLocalRuntimeDispatchOnly() {
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills");
		handlers::tools_shared::ToolsSharedHandlers::RegisterToolsList(m_dispatcher, m_toolRegistry);
		handlers::tools_shared::ToolsSharedHandlers::RegisterToolsCatalog(m_dispatcher, m_toolRegistry);
		RegisterGatewayRegistryIntrospectionHandlers();
		if (!m_runtimeHandlersInitialized) {
			RegisterRuntimeHandlers();
			m_runtimeHandlersInitialized = true;
		}
		m_dispatchInitialized = true;

		m_running = true;
		return true;
	}

	bool GatewayHost::CreateRuntimeState(
		const blazeclaw::config::GatewayConfig& config) {
		if (config.bindAddress.empty() || config.port == 0) {
			m_lastWarning = "Invalid gateway bind configuration.";
			return false;
		}

		m_bindAddress = ToNarrow(config.bindAddress);
		m_port = config.port;
		m_runtimeGatewayBind = m_bindAddress;
		m_runtimeGatewayPort = m_port;

		if (!m_stageRuntimeHost) {
			m_stageRuntimeHost = std::make_unique<GatewayHostEx>(
				GatewayHostExDependencies{
					.legacyHost = this,
					.stagePipeline = &m_chatRunPipelineOrchestrator,
				});
		}

		return true;
	}

	bool GatewayHost::StartRuntimeServices() {
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		m_runtimeDeepSeekApiKey = ReadEnvironmentVariable("DEEPSEEK_API_KEY");
		const std::string deepSeekBaseUrl =
			ReadEnvironmentVariable("DEEPSEEK_BASE_URL");
		if (!deepSeekBaseUrl.empty()) {
			m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
		}

		if (!m_runtimeDeepSeekApiKey.empty() &&
			(m_runtimeAgentModel == GatewayModel::kDefaultModelId ||
				m_runtimeAgentModel == GatewayModel::kReasonerModelId)) {
			m_runtimeAgentModel = m_runtimeDeepSeekDefaultModel;
		}

		const std::string catalogPath = ResolveExtensionsCatalogPath();
		m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills-bundled");
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("blazeclaw/skills-openclaw-original");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills");
		m_toolRegistry.LoadSkillToolsFromDirectory("skills-openclaw-original");

		m_extensionLifecycle.LoadCatalog(catalogPath);
		m_extensionLifecycle.ActivateAll(m_toolRegistry);
		const auto* extensionRegistry =
			m_pluginRuntimeState.RequireActiveRegistry(
				&m_extensionLifecycle.GetExtensions(),
				catalogPath,
				std::filesystem::current_path().string(),
				PluginRuntimeSubagentMode::GatewayBindable);
		for (const auto& extension : *extensionRegistry) {
			m_pluginRuntimeState.RecordImportedPluginId(extension.id);
		}
		m_pluginRuntimeState.ActivateRuntimeRegistry(
			extensionRegistry,
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable,
			true,
			true);
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

		return true;
	}

	bool GatewayHost::AttachTransportRuntime() {
		m_transport.SetHttpAuthCallbacks(
			GatewayHttpAuthPolicyCallbacks{
				.authorizeBearer = [this](
					const std::string& token,
					const GatewayHttpAuthRequestContext& context,
					std::string& failureReasonOut) {
						if (context.browserOriginPolicy == "origin_rejected") {
							failureReasonOut = "unauthorized";
							return false;
						}

						if (token.empty()) {
							failureReasonOut = "unauthorized";
							return false;
						}

						const std::vector<NodePairingPairedNode> paired =
							m_nodePairingService.ListPairedNodes();
						for (const auto& node : paired) {
							if (m_nodePairingService.VerifyNodeToken(
								node.declared.nodeId,
								token,
								nullptr)) {
								return true;
							}
						}

						failureReasonOut = "unauthorized";
						return false;
					},
				.checkRateLimit = [this](
					const GatewayHttpAuthRequestContext& context,
					std::string& failureReasonOut) {
						const std::string remoteKey =
							context.remoteIp.empty() ? "__unknown_remote_ip__" : context.remoteIp;
						auto& attempts = m_httpAuthBearerAttemptsByRemoteIp[remoteKey];
						const std::uint64_t nowMs = GatewayEpochMilliseconds();
						while (!attempts.empty() &&
							(nowMs - attempts.front()) > m_httpAuthRateLimitWindowMs) {
							attempts.pop_front();
						}

						if (attempts.size() >= m_httpAuthRateLimitMaxAttemptsPerWindow) {
							failureReasonOut = "rate_limited";
							return false;
						}

						attempts.push_back(nowMs);
						return true;
					},
				.authorizeCanvasCapability = [this](
					const std::string& capability,
					const GatewayHttpAuthRequestContext&,
					std::string& failureReasonOut) {
						if (m_nodeCanvasCapabilityService.VerifyCapabilityAndRefreshTtl(capability)) {
							return true;
						}
						failureReasonOut = "unauthorized";
						return false;
					},
				.observeDecision = nullptr,
			});
		return true;
	}

	bool GatewayHost::StartRuntimeSubscriptions() {
		return true;
	}

	bool GatewayHost::FinalizeRuntimeInitialization() {
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "chat.send",
				.label = "Chat Send",
				.category = "messaging",
				.enabled = true,
			},
			[this](
				const std::string& requestedTool,
				const std::optional<std::string>& argsJson) {
					const protocol::RequestFrame runtimeRequest{
						.id = "tool-runtime-" + std::to_string(GatewayEpochMilliseconds()),
						.method = requestedTool,
						.paramsJson = argsJson,
					};

					const protocol::ResponseFrame runtimeResponse =
						RouteRequest(runtimeRequest);
					if (!runtimeResponse.ok) {
						const std::string detail = runtimeResponse.error.has_value()
							? (runtimeResponse.error->code +
								":" +
								runtimeResponse.error->message)
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
			[this](
				const std::string& requestedTool,
				const std::optional<std::string>& argsJson) {
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
							if (ToLowerCopy(messageJson).find(loweredQuery) ==
								std::string::npos) {
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

		std::string methodSurfaceViolation;
		if (!VerifyRuntimeMethodSurfaceInvariants(methodSurfaceViolation)) {
			EmitTelemetryEvent(
				"gateway.method_surface.invariant_failed",
				std::string("{\"reason\":") + JsonString(methodSurfaceViolation) + "}");
		}

		m_initialized = true;
		return true;
	}

	bool GatewayHost::InitializeRuntime(const blazeclaw::config::GatewayConfig& config) {
		if (!BootstrapCreateRuntimeState(config)) {
			return false;
		}
		if (!BootstrapStartRuntimeServices()) {
			return false;
		}
		if (!BootstrapAttachTransportHandlers()) {
			return false;
		}
		if (!BootstrapStartRuntimeSubscriptions()) {
			return false;
		}
		return BootstrapFinalizeRuntimeInitialization();
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

	void GatewayHost::NotifyPluginGlobalStopPrelude() {
		PluginHostAdapter::UnloadAllLoadedExtensionRuntimes();
		m_pluginRuntimeState.RecordGlobalStopPreludeTransition();
	}

	void GatewayHost::Stop() {
		m_transport.Stop();
		// Deactivate registered extension tools and clear approval state
		m_extensionLifecycle.DeactivateAll(m_toolRegistry);
		m_pluginRuntimeState.DeactivateRuntimeRegistry();
		PersistTaskDeltas();
		m_running = false;
		m_initialized = false;
		m_dispatchInitialized = false;
		m_runtimeHandlersInitialized = false;
		m_fixtureParityValidated = false;
		m_runtimeContext = {};
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

	void GatewayHost::SetConfigSchemaGetCallback(
		ConfigSchemaGetCallback callback) {
		m_configSchemaGetCallback = std::move(callback);
	}

	void GatewayHost::SetConfigSchemaLookupCallback(
		ConfigSchemaLookupCallback callback) {
		m_configSchemaLookupCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddedOrchestrationPath(
		const std::string& path) {
		const std::string normalized = ToLowerCopy(json::Trim(path));
		m_stagePipelineFeatureEnabled = true;
		m_stagePipelineRolloutCohort = "stage_pipeline_full";
		if (normalized == "runtime_orchestration") {
			m_embeddedOrchestrationPath = normalized;
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "compat_runtime_orchestration";
			return;
		}
		if (normalized == "legacy_only" ||
			normalized == "stage_pipeline_off") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "stage_pipeline_off";
			return;
		}
		if (normalized == "stage_pipeline_canary") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = true;
			m_stagePipelineRolloutCohort = "canary";
			return;
		}
		if (normalized == "stage_pipeline_full") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = true;
			m_stagePipelineRolloutCohort = "stage_pipeline_full";
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

	void GatewayHost::SetNodeParityRuntimeFlags(
		bool enabled,
		bool diagnosticsEnabled,
		const std::string& rolloutMode) {
		m_runtimeNodeParityEnabled = enabled;
		m_runtimeNodeParityDiagnosticsEnabled = diagnosticsEnabled;
		m_runtimeNodeParityRolloutMode = rolloutMode.empty() ? "monitor" : rolloutMode;
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

			std::uint64_t loadActivity = 0;
			for (const auto& d : normalizedDeltas) {
				loadActivity = (std::max)(loadActivity, d.startedAtMs);
				loadActivity = (std::max)(loadActivity, d.completedAtMs);
			}
			if (loadActivity == 0) {
				loadActivity = 1;
			}

			const bool upserted = m_taskDeltaRepository.Upsert(
				runId,
				normalizedDeltas,
				loadActivity);
			(void)upserted;
		}

		const std::size_t evictedFromLoad =
			m_taskDeltaRepository.EnforceRetentionLimit(m_taskDeltasRetentionLimit);
		if (evictedFromLoad > 0) {
			EmitTelemetryEvent(
				"gateway.taskdelta.retention.evicted",
				std::string("{\"evictedRuns\":") + std::to_string(evictedFromLoad) +
				",\"remainingRuns\":" + std::to_string(m_taskDeltaRepository.Size()) +
				",\"reason\":\"persistence_load\"}");
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

	void GatewayHost::SetParityLifecycleExportCallback(
		ParityLifecycleExportCallback callback) {
		m_parityLifecycleExport = std::move(callback);
	}

	std::string GatewayHost::ExportParityLifecycleTraceJson() const {
		if (!m_parityLifecycleExport) {
			return
				"{\"ok\":false,\"error\":"
				"\"gateway.parity.lifecycle_export_unconfigured\""
				",\"schemaVersion\":" +
				std::to_string(
					blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion) +
				"}";
		}
		return std::string("{\"ok\":true,\"schemaVersion\":") +
			std::to_string(
				blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion) +
			",\"contract\":" +
			m_parityLifecycleExport() +
			"}";
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
		m_transportRecipientRegistry.PruneDisconnected(connectionId);
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
		return protocol::EncodeValidatedEvent(
			"gateway.tick",
			"{\"ts\":" + std::to_string(timestampMs) +
			",\"running\":" + std::string(IsRunning() ? "true" : "false") +
			",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}",
			seq,
			"tick");
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

		return protocol::EncodeValidatedEvent(
			"gateway.channels.accounts.update",
			"{\"accounts\":" + accountsJson + "}",
			seq,
			"channels.accounts.update");
	}

	std::string GatewayHost::BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const {
		const AgentEntry agent = m_agentRegistry.Get(agentId);

		return protocol::EncodeValidatedEvent(
			"gateway.agent.update",
			"{\"agentId\":\"" + agent.id + "\",\"agent\":" + SerializeAgent(agent) + "}",
			seq,
			"agent.update");
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

		return protocol::EncodeValidatedEvent(
			"gateway.tools.catalog.update",
			"{\"tools\":" + toolsJson + "}",
			seq,
			"tools.catalog.update");
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

		return protocol::EncodeValidatedEvent(
			"gateway.channels.update",
			"{\"channels\":" + channelsJson + "}",
			seq,
			"channels.update");
	}

	std::string GatewayHost::BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const {
		const SessionEntry session = m_sessionRegistry.Resolve(sessionId);

		return protocol::EncodeValidatedEvent(
			"gateway.session.reset",
			"{\"sessionId\":\"" + session.id + "\",\"session\":" + SerializeSession(session) + "}",
			seq,
			"session.reset");
	}

	std::string GatewayHost::BuildHealthEventFrame(std::uint64_t seq) const {
		return protocol::EncodeValidatedEvent(
			"gateway.health",
			"{\"status\":\"ok\",\"running\":true}",
			seq,
			"health");
	}

	std::string GatewayHost::BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const {
		return protocol::EncodeValidatedEvent(
			"gateway.shutdown",
			"{\"reason\":\"" + reason + "\",\"graceful\":true,\"seq\":" + std::to_string(seq) + "}",
			seq,
			"shutdown");
	}

	std::string GatewayHost::HandleInboundText(const std::string& inboundJson) const {
		protocol::RequestFrame request;
		std::string decodeError;
		if (!protocol::TryDecodeRequestFrame(inboundJson, request, decodeError)) {
			const protocol::ResponseFrame errorResponse = protocol::ErrorResponse(
				protocol::RequestFrame{},
				protocol::ErrorShape{
					.code = "invalid_frame",
					.message = decodeError,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(errorResponse);
		}

		protocol::SchemaValidationIssue validationIssue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateRequest(request, validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
					.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		const auto policyError = m_requestPolicyGuard.Evaluate(
			request,
			GatewayRequestPolicyGuard::Context{
				.dispatchInitialized = m_dispatchInitialized,
				.hostRunning = m_running,
			});
		if (policyError.has_value()) {
			const protocol::ResponseFrame policyErrorResponse =
				protocol::ErrorResponse(request, std::move(policyError.value()));

			return protocol::EncodeResponseFrame(policyErrorResponse);
		}

		const protocol::ResponseFrame routedResponse = RouteRequest(request);
		if (!protocol::GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			request.method,
			routedResponse,
			validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_invalid_response" : validationIssue.code,
					.message = validationIssue.message.empty()
						? "Handler response failed schema validation."
						: validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

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
		   .requestId = request.id,
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
			const protocol::ResponseFrame stageResponse =
				mutableThis->m_stageRuntimeHost->RouteRequest(request);
			if (!stageResponse.ok &&
				stageResponse.error.has_value() &&
				stageResponse.error->code == "stage_host_unavailable") {
				EmitTelemetryEvent(
					"gateway.host.route.decision",
					BuildGatewayHostRouteDecisionPayload(
						request.method,
						"legacy",
						"fallback_stage_host_runtime_unavailable",
						routeDecision.selectedCohort,
						true));
				return RouteRequestLegacy(request);
			}

			return stageResponse;
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

	void GatewayHost::BindRuntimeContext() noexcept {
		m_runtimeContext.dispatcher = &m_dispatcher;
		m_runtimeContext.transport = &m_transport;
		m_runtimeContext.agentRegistry = &m_agentRegistry;
		m_runtimeContext.channelRegistry = &m_channelRegistry;
		m_runtimeContext.sessionRegistry = &m_sessionRegistry;
		m_runtimeContext.toolRegistry = &m_toolRegistry;
		m_runtimeContext.transportRecipientRegistry = &m_transportRecipientRegistry;
		m_runtimeContext.chatRunPipeline = &m_chatRunPipelineOrchestrator;
		m_runtimeContext.eventFanout = &m_eventFanoutService;
		m_runtimeContext.nodePairing = &m_nodePairingService;
		m_runtimeContext.nodeCatalog = &m_nodeCatalogService;
		m_runtimeContext.nodeCanvas = &m_nodeCanvasCapabilityService;
		m_runtimeContext.nodePending = &m_nodePendingActionQueue;
		m_runtimeContext.nodeWake = &m_nodeWakeService;
	}

	void GatewayHost::RegisterDefaultHandlers() {
		BindRuntimeContext();
		GatewayHostRegistration::RegisterDefaultHandlerSequence(*this);
	}

	bool GatewayHost::VerifyRuntimeMethodSurfaceInvariants(std::string& violationOut) const {
		std::unordered_set<std::string> pluginRpcDeduped;
		for (const auto& manifest : m_extensionLifecycle.GetExtensions()) {
			for (const std::string& method : manifest.gatewayRpcMethods) {
				if (!method.empty()) {
					pluginRpcDeduped.insert(method);
				}
			}
		}

		const std::vector<std::string> pluginRpcUnion(
			pluginRpcDeduped.begin(),
			pluginRpcDeduped.end());

		return GatewayRuntimeMethodSurfaceInvariantsHold(
			m_dispatcher,
			pluginRpcUnion,
			violationOut);
	}

	bool GatewayHost::PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry(
		std::string& outDeltaJson) {
		if (!m_initialized) {
			outDeltaJson = R"({"ok":false,"reason":"runtime_not_initialized"})";
			return false;
		}

		const auto sortNames = [](const GatewayMethodDispatcher& dispatcher) {
			auto names = dispatcher.RegisteredMethods();
			std::sort(names.begin(), names.end());
			return names;
			};

		const std::vector<std::string> before = sortNames(m_dispatcher);
		const std::string catalogPath = ResolveExtensionsCatalogPath();

		m_extensionLifecycle.DeactivateAll(m_toolRegistry);
		m_pluginRuntimeState.DeactivateRuntimeRegistry();
		m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
		m_extensionLifecycle.LoadCatalog(catalogPath);
		m_extensionLifecycle.ActivateAll(m_toolRegistry);
		const auto* extensionRegistry = m_pluginRuntimeState.RequireActiveRegistry(
			&m_extensionLifecycle.GetExtensions(),
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable);
		for (const auto& extension : *extensionRegistry) {
			m_pluginRuntimeState.RecordImportedPluginId(extension.id);
		}
		m_pluginRuntimeState.ActivateRuntimeRegistry(
			extensionRegistry,
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable,
			true,
			true);
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);

		const std::vector<std::string> after = sortNames(m_dispatcher);
		std::vector<std::string> added;
		std::vector<std::string> removed;
		added.reserve(after.size());
		removed.reserve(before.size());
		std::set_difference(
			after.begin(),
			after.end(),
			before.begin(),
			before.end(),
			std::back_inserter(added));
		std::set_difference(
			before.begin(),
			before.end(),
			after.begin(),
			after.end(),
			std::back_inserter(removed));

		std::string addedJson = "[";
		for (std::size_t i = 0; i < added.size() && i < 12; ++i) {
			if (i > 0) {
				addedJson += ",";
			}
			addedJson += JsonString(added[i]);
		}
		addedJson += "]";
		std::string removedJson = "[";
		for (std::size_t i = 0; i < removed.size() && i < 12; ++i) {
			if (i > 0) {
				removedJson += ",";
			}
			removedJson += JsonString(removed[i]);
		}
		removedJson += "]";

		outDeltaJson =
			std::string("{\"ok\":true,\"phase\":\"deferred_extension_catalog_reload\"") +
			",\"beforeCount\":" + std::to_string(before.size()) +
			",\"afterCount\":" + std::to_string(after.size()) +
			",\"addedCount\":" + std::to_string(added.size()) +
			",\"removedCount\":" + std::to_string(removed.size()) +
			",\"addedSample\":" + std::move(addedJson) +
			",\"removedSample\":" + std::move(removedJson) + "}";

		EmitTelemetryEvent("gateway.method_surface.extension_reload", outDeltaJson);
		return true;
	}

} // namespace blazeclaw::gateway
