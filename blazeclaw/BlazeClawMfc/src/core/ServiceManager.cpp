#include "pch.h"
#include "ServiceManager.h"

#include "../gateway/GatewayProtocolModels.h"

#include <filesystem>
#include <unordered_map>

namespace blazeclaw::core {

namespace {

std::string ToNarrow(const std::wstring& value) {
  std::string output;
  output.reserve(value.size());

  for (const wchar_t ch : value) {
    output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
  }

  return output;
}

} // namespace

ServiceManager::ServiceManager() = default;

blazeclaw::gateway::SkillsCatalogGatewayState ServiceManager::BuildGatewaySkillsState() const {
  blazeclaw::gateway::SkillsCatalogGatewayState gatewaySkillsState;
  gatewaySkillsState.entries.reserve(m_skillsCatalog.entries.size());

  std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
  for (const auto& eligibility : m_skillsEligibility.entries) {
    eligibilityByName.emplace(eligibility.skillName, eligibility);
  }

  std::unordered_map<std::wstring, SkillsCommandSpec> commandsBySkill;
  for (const auto& command : m_skillsCommands.commands) {
    commandsBySkill.emplace(command.skillName, command);
  }

  std::unordered_map<std::wstring, SkillsInstallPlanEntry> installBySkill;
  for (const auto& plan : m_skillsInstall.entries) {
    installBySkill.emplace(plan.skillName, plan);
  }

  for (const auto& entry : m_skillsCatalog.entries) {
    const auto eligibilityIt = eligibilityByName.find(entry.skillName);
    const bool hasEligibility = eligibilityIt != eligibilityByName.end();

    std::string commandName;
    const auto commandIt = commandsBySkill.find(entry.skillName);
    if (commandIt != commandsBySkill.end()) {
      commandName = ToNarrow(commandIt->second.name);
    }

    std::string installKind;
    std::string installCommand;
    std::string installReason;
    bool installExecutable = false;
    const auto installIt = installBySkill.find(entry.skillName);
    if (installIt != installBySkill.end()) {
      installKind = ToNarrow(installIt->second.kind);
      installCommand = ToNarrow(installIt->second.command);
      installReason = ToNarrow(installIt->second.reason);
      installExecutable = installIt->second.executable;
    }

    gatewaySkillsState.entries.push_back(
        blazeclaw::gateway::SkillsCatalogGatewayEntry{
            .name = ToNarrow(entry.skillName),
            .skillKey = hasEligibility ? ToNarrow(eligibilityIt->second.skillKey)
                                       : ToNarrow(entry.skillName),
            .commandName = commandName,
            .installKind = installKind,
            .installCommand = installCommand,
            .installExecutable = installExecutable,
            .installReason = installReason,
            .description = ToNarrow(entry.description),
            .source = ToNarrow(SkillsCatalogService::SourceKindLabel(entry.sourceKind)),
            .precedence = entry.precedence,
            .eligible = hasEligibility ? eligibilityIt->second.eligible : false,
            .disabled = hasEligibility ? eligibilityIt->second.disabled : false,
            .blockedByAllowlist =
                hasEligibility ? eligibilityIt->second.blockedByAllowlist : false,
            .disableModelInvocation =
                hasEligibility ? eligibilityIt->second.disableModelInvocation : false,
            .validFrontmatter = entry.validFrontmatter,
            .validationErrorCount = entry.validationErrors.size(),
        });
  }

  gatewaySkillsState.rootsScanned = m_skillsCatalog.diagnostics.rootsScanned;
  gatewaySkillsState.rootsSkipped = m_skillsCatalog.diagnostics.rootsSkipped;
  gatewaySkillsState.oversizedSkillFiles =
      m_skillsCatalog.diagnostics.oversizedSkillFiles;
  gatewaySkillsState.invalidFrontmatterFiles =
      m_skillsCatalog.diagnostics.invalidFrontmatterFiles;
  gatewaySkillsState.warningCount = m_skillsCatalog.diagnostics.warnings.size();
  gatewaySkillsState.eligibleCount = m_skillsEligibility.eligibleCount;
  gatewaySkillsState.disabledCount = m_skillsEligibility.disabledCount;
  gatewaySkillsState.blockedByAllowlistCount =
      m_skillsEligibility.blockedByAllowlistCount;
  gatewaySkillsState.missingRequirementsCount =
      m_skillsEligibility.missingRequirementsCount;
  gatewaySkillsState.promptIncludedCount = m_skillsPrompt.includedCount;
  gatewaySkillsState.promptChars = m_skillsPrompt.promptChars;
  gatewaySkillsState.promptTruncated = m_skillsPrompt.truncated;
  gatewaySkillsState.snapshotVersion = m_skillsWatch.version;
  gatewaySkillsState.watchEnabled = m_skillsWatch.watchEnabled;
  gatewaySkillsState.watchDebounceMs = m_skillsWatch.debounceMs;
  gatewaySkillsState.watchReason = ToNarrow(m_skillsWatch.reason);
  gatewaySkillsState.prompt = ToNarrow(m_skillsPrompt.prompt);
  gatewaySkillsState.sandboxSyncOk = m_skillsSync.success;
  gatewaySkillsState.sandboxSynced = m_skillsSync.copiedSkills;
  gatewaySkillsState.sandboxSkipped = m_skillsSync.skippedSkills;
  gatewaySkillsState.envAllowed = m_skillsEnvOverrides.allowedCount;
  gatewaySkillsState.envBlocked = m_skillsEnvOverrides.blockedCount;
  gatewaySkillsState.installExecutableCount = m_skillsInstall.executableCount;
  gatewaySkillsState.installBlockedCount = m_skillsInstall.blockedCount;
  gatewaySkillsState.scanInfoCount = m_skillSecurityScan.infoCount;
  gatewaySkillsState.scanWarnCount = m_skillSecurityScan.warnCount;
  gatewaySkillsState.scanCriticalCount = m_skillSecurityScan.criticalCount;
  gatewaySkillsState.scanScannedFiles = m_skillSecurityScan.scannedFileCount;
  return gatewaySkillsState;
}

void ServiceManager::RefreshSkillsState(
    const blazeclaw::config::AppConfig& config,
    const bool forceRefresh,
    const std::wstring& reason) {
  m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
      std::filesystem::current_path(),
      config);
  m_skillsEligibility = m_skillsEligibilityService.Evaluate(
      m_skillsCatalog,
      config);
  m_skillsPrompt = m_skillsPromptService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsCommands = m_skillsCommandService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility);
  m_skillsSync = m_skillsSyncService.SyncToSandbox(
      std::filesystem::current_path(),
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsEnvOverrides = m_skillsEnvOverrideService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsInstall = m_skillsInstallService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillSecurityScan = m_skillSecurityScanService.BuildSnapshot(
      m_skillsCatalog,
      m_skillsEligibility,
      config);
  m_skillsEnvOverrideService.Apply(m_skillsEnvOverrides);
  m_skillsWatch = m_skillsWatchService.Observe(
      m_skillsCatalog,
      config,
      forceRefresh,
      reason);
}

bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
  if (m_running) {
    return true;
  }

  m_activeConfig = config;
  m_agentsScope = m_agentsCatalogService.BuildSnapshot(
      std::filesystem::current_path(),
      m_activeConfig);
  m_agentsWorkspace = m_agentsWorkspaceService.BuildSnapshot(m_agentsScope);
  m_agentsToolPolicy = m_agentsToolPolicyService.BuildSnapshot(
      m_agentsScope,
      m_activeConfig);
  m_agentsShellRuntimeService.Configure(m_activeConfig);
  m_agentsModelRoutingService.Configure(m_activeConfig);
  m_modelRouting = m_agentsModelRoutingService.Snapshot();
  m_agentsAuthProfileService.Configure(m_activeConfig);
  m_agentsAuthProfileService.Initialize(std::filesystem::current_path());
  m_authProfiles = m_agentsAuthProfileService.Snapshot(1735690000000);
  m_sandbox = m_agentsSandboxService.BuildSnapshot(m_agentsScope, m_activeConfig);
  m_agentsTranscriptSafetyService.Configure(m_activeConfig);
  m_subagentRegistryService.Configure(m_activeConfig);
  m_subagentRegistryService.Initialize(std::filesystem::current_path());
  m_subagentRegistry = m_subagentRegistryService.Snapshot();
  m_lastAcpDecision = m_acpSpawnService.Evaluate(
      m_activeConfig,
      AcpSpawnRequest{
          .requesterSessionId = "main",
          .requesterAgentId = "default",
          .targetAgentId = "",
          .threadRequested = false,
          .requesterSandboxed = false,
      });
  m_piEmbeddedService.Configure(m_activeConfig);
  RefreshSkillsState(m_activeConfig, true, L"startup");

  std::wstring fixtureError;
  const std::vector<std::filesystem::path> fixtureCandidates = {
      std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"agents",
      std::filesystem::current_path() / L"fixtures" / L"agents",
      std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
      std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
  };

  for (const auto& candidate : fixtureCandidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec) || ec) {
      continue;
    }

    const std::wstring candidateName = candidate.filename().wstring();
    if (candidateName == L"agents") {
      if (!m_agentsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-scope fixture validation failed: " + fixtureError);
      }

      if (!m_agentsWorkspaceService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-workspace fixture validation failed: " + fixtureError);
      }

      if (!m_agentsToolPolicyService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-tool-policy fixture validation failed: " + fixtureError);
      }

      if (!m_agentsShellRuntimeService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-shell-runtime fixture validation failed: " + fixtureError);
      }

      if (!m_agentsModelRoutingService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-model-routing fixture validation failed: " + fixtureError);
      }

      if (!m_agentsAuthProfileService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-auth-profile fixture validation failed: " + fixtureError);
      }

      if (!m_agentsSandboxService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-sandbox fixture validation failed: " + fixtureError);
      }

      if (!m_agentsTranscriptSafetyService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-transcript fixture validation failed: " + fixtureError);
      }

      if (!m_subagentRegistryService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-subagent fixture validation failed: " + fixtureError);
      }

      if (!m_acpSpawnService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-acp fixture validation failed: " + fixtureError);
      }

      if (!m_piEmbeddedService.ValidateFixtureScenarios(candidate, fixtureError)) {
        m_skillsCatalog.diagnostics.warnings.push_back(
            L"agents-embedded fixture validation failed: " + fixtureError);
      }

      continue;
    }

    if (!m_skillsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-catalog fixture validation failed: " + fixtureError);
    }

    if (!m_skillsEligibilityService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-eligibility fixture validation failed: " + fixtureError);
    }

    if (!m_skillsPromptService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-prompt fixture validation failed: " + fixtureError);
    }

    if (!m_skillsCommandService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-command fixture validation failed: " + fixtureError);
    }

    if (!m_skillsWatchService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-watch fixture validation failed: " + fixtureError);
    }

    if (!m_skillsSyncService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-sync fixture validation failed: " + fixtureError);
    }

    if (!m_skillsEnvOverrideService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-env fixture validation failed: " + fixtureError);
    }

    if (!m_skillsInstallService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-install fixture validation failed: " + fixtureError);
    }

    if (!m_skillSecurityScanService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-scan fixture validation failed: " + fixtureError);
    }

    break;
  }

  m_gatewayHost.SetSkillsCatalogState(BuildGatewaySkillsState());
  m_gatewayHost.SetSkillsRefreshCallback([this]() {
    RefreshSkillsState(m_activeConfig, true, L"manual-refresh");
    return BuildGatewaySkillsState();
  });

  m_gatewayHost.SetChatRuntimeCallback([this](
      const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request) {
    const auto modelSelection = m_agentsModelRoutingService.SelectModel(
        m_activeConfig.agent.model.empty()
            ? std::string()
            : ToNarrow(m_activeConfig.agent.model),
        "chat.send");

    const auto embeddedRun = m_piEmbeddedService.QueueRun(
        EmbeddedRunRequest{
            .sessionId = request.sessionKey.empty() ? "main" : request.sessionKey,
            .agentId = "default",
            .message = request.message,
        });

    if (!embeddedRun.accepted) {
      m_agentsModelRoutingService.RecordFailover(
          modelSelection.selectedModel,
          embeddedRun.reason,
          embeddedRun.startedAtMs == 0 ? 1735689800000 : embeddedRun.startedAtMs);
      return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
          .ok = false,
          .assistantText = {},
          .modelId = modelSelection.selectedModel,
          .errorCode = "embedded_run_rejected",
          .errorMessage = embeddedRun.reason,
      };
    }

    const std::string assistantText = request.message.empty()
        ? "Received image attachment."
        : ("Model(" + modelSelection.selectedModel + "): " + request.message);

    const bool completed = m_piEmbeddedService.CompleteRun(
        embeddedRun.runId,
        "completed",
        embeddedRun.startedAtMs + 1);
    if (!completed) {
      m_agentsModelRoutingService.RecordFailover(
          modelSelection.selectedModel,
          "embedded_completion_failed",
          embeddedRun.startedAtMs + 1);
      return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
          .ok = false,
          .assistantText = {},
          .modelId = modelSelection.selectedModel,
          .errorCode = "embedded_completion_failed",
          .errorMessage = "embedded completion failed",
      };
    }

    return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
        .ok = true,
        .assistantText = assistantText,
        .modelId = modelSelection.selectedModel,
        .errorCode = {},
        .errorMessage = {},
    };
  });

  const bool gatewayStarted = m_gatewayHost.Start(config.gateway);
  m_running = gatewayStarted;
  return m_running;
}

void ServiceManager::Stop() {
  m_skillsEnvOverrideService.RevertAll();
  m_gatewayHost.Stop();
  m_running = false;
}

bool ServiceManager::IsRunning() const noexcept {
  return m_running;
}

const FeatureRegistry& ServiceManager::Registry() const noexcept {
  return m_registry;
}

const AgentScopeSnapshot& ServiceManager::AgentsScope() const noexcept {
  return m_agentsScope;
}

const AgentsWorkspaceSnapshot& ServiceManager::AgentsWorkspace() const noexcept {
  return m_agentsWorkspace;
}

const SubagentRegistrySnapshot& ServiceManager::SubagentRegistry() const noexcept {
  return m_subagentRegistry;
}

const AcpSpawnDecision& ServiceManager::LastAcpDecision() const noexcept {
  return m_lastAcpDecision;
}

std::size_t ServiceManager::ActiveEmbeddedRuns() const noexcept {
  return m_piEmbeddedService.ActiveRuns();
}

const AgentsToolPolicySnapshot& ServiceManager::ToolPolicy() const noexcept {
  return m_agentsToolPolicy;
}

std::size_t ServiceManager::ShellProcessCount() const noexcept {
  return m_agentsShellRuntimeService.ListProcesses().size();
}

const ModelRoutingSnapshot& ServiceManager::ModelRouting() const noexcept {
  return m_modelRouting;
}

const AuthProfileSnapshot& ServiceManager::AuthProfiles() const noexcept {
  return m_authProfiles;
}

const SandboxSnapshot& ServiceManager::Sandbox() const noexcept {
  return m_sandbox;
}

std::string ServiceManager::BuildOperatorDiagnosticsReport() const {
  const auto featureStateLabel = [](const FeatureState state) {
    switch (state) {
      case FeatureState::Implemented:
        return "implemented";
      case FeatureState::InProgress:
        return "in_progress";
      case FeatureState::Planned:
      default:
        return "planned";
    }
  };

  std::size_t implementedCount = 0;
  std::size_t inProgressCount = 0;
  std::size_t plannedCount = 0;
  for (const auto& feature : m_registry.Features()) {
    if (feature.state == FeatureState::Implemented) {
      ++implementedCount;
      continue;
    }

    if (feature.state == FeatureState::InProgress) {
      ++inProgressCount;
      continue;
    }

    ++plannedCount;
  }

  const auto routing = ModelRouting();
  const auto auth = AuthProfiles();
  const auto sandbox = Sandbox();
  const auto provider = ToNarrow(m_activeConfig.embeddings.provider);
  const auto executionMode = ToNarrow(m_activeConfig.embeddings.executionMode);
  const bool modelPathConfigured =
      !m_activeConfig.embeddings.modelPath.empty();
  const bool tokenizerPathConfigured =
      !m_activeConfig.embeddings.tokenizerPath.empty();
  const bool configFeatureImplemented =
      m_registry.IsImplemented(L"embeddings-config-foundation");

  std::string report =
      "{\"runtime\":{\"running\":" + std::string(m_running ? "true" : "false") +
      ",\"gatewayWarning\":\"" + m_gatewayHost.LastWarning() + "\"},"
      "\"agents\":{\"count\":" + std::to_string(m_agentsScope.entries.size()) +
      ",\"defaultAgent\":\"" + ToNarrow(m_agentsScope.defaultAgentId) + "\"},"
      "\"subagents\":{\"active\":" + std::to_string(m_subagentRegistry.activeRuns) +
      ",\"pendingAnnounce\":" + std::to_string(m_subagentRegistry.pendingAnnounce) + "},"
      "\"acp\":{\"lastAllowed\":" +
      std::string(m_lastAcpDecision.allowed ? "true" : "false") +
      ",\"reason\":\"" + m_lastAcpDecision.reason + "\"},"
      "\"embedded\":{\"activeRuns\":" + std::to_string(ActiveEmbeddedRuns()) + "},"
      "\"tools\":{\"policyEntries\":" + std::to_string(m_agentsToolPolicy.entries.size()) +
      ",\"shellProcesses\":" + std::to_string(ShellProcessCount()) + "},"
      "\"modelAuth\":{\"primary\":\"" + routing.primaryModel +
      "\",\"fallback\":\"" + routing.fallbackModel +
      "\",\"failovers\":" + std::to_string(routing.failoverHistory.size()) +
      ",\"authProfiles\":" + std::to_string(auth.entries.size()) + "},"
      "\"sandbox\":{\"enabledCount\":" + std::to_string(sandbox.enabledCount) +
      ",\"browserEnabledCount\":" + std::to_string(sandbox.browserEnabledCount) + "},"
      "\"embeddings\":{\"enabled\":" +
      std::string(m_activeConfig.embeddings.enabled ? "true" : "false") +
      ",\"provider\":\"" + provider +
      "\",\"executionMode\":\"" + executionMode +
      "\",\"dimension\":" + std::to_string(m_activeConfig.embeddings.dimension) +
      ",\"maxSequenceLength\":" +
      std::to_string(m_activeConfig.embeddings.maxSequenceLength) +
      ",\"modelPathConfigured\":" +
      std::string(modelPathConfigured ? "true" : "false") +
      ",\"tokenizerPathConfigured\":" +
      std::string(tokenizerPathConfigured ? "true" : "false") +
      ",\"configFeatureImplemented\":" +
      std::string(configFeatureImplemented ? "true" : "false") + "},"
      "\"skills\":{\"catalogEntries\":" + std::to_string(m_skillsCatalog.entries.size()) +
      ",\"promptIncluded\":" + std::to_string(m_skillsPrompt.includedCount) + "},"
      "\"features\":{\"implemented\":" + std::to_string(implementedCount) +
      ",\"inProgress\":" + std::to_string(inProgressCount) +
      ",\"planned\":" + std::to_string(plannedCount) +
      ",\"registryState\":\"" + featureStateLabel(
          m_registry.Features().empty() ? FeatureState::Planned : m_registry.Features().front().state) +
      "\"}}";

  return report;
}

const SkillsCatalogSnapshot& ServiceManager::SkillsCatalog() const noexcept {
  return m_skillsCatalog;
}

const SkillsEligibilitySnapshot& ServiceManager::SkillsEligibility() const noexcept {
  return m_skillsEligibility;
}

const SkillsPromptSnapshot& ServiceManager::SkillsPrompt() const noexcept {
  return m_skillsPrompt;
}

std::string ServiceManager::InvokeGatewayMethod(
    const std::string& method,
    const std::optional<std::string>& paramsJson) const {
  const blazeclaw::gateway::protocol::RequestFrame request{
      .id = "ui-probe",
      .method = method,
      .paramsJson = paramsJson,
  };

  const auto response = RouteGatewayRequest(request);
  if (response.ok) {
    return response.payloadJson.has_value() ? response.payloadJson.value()
                                            : "ok";
  }

  if (!response.error.has_value()) {
    return "error_unknown";
  }

  const auto& error = response.error.value();
  return error.code + ":" + error.message;
}

blazeclaw::gateway::protocol::ResponseFrame ServiceManager::RouteGatewayRequest(
    const blazeclaw::gateway::protocol::RequestFrame& request) const {
  if (!m_running) {
    return blazeclaw::gateway::protocol::ResponseFrame{
        .id = request.id,
        .ok = false,
        .payloadJson = std::nullopt,
        .error = blazeclaw::gateway::protocol::ErrorShape{
            .code = "service_not_running",
            .message = "Service manager is not running.",
            .detailsJson = std::nullopt,
            .retryable = false,
            .retryAfterMs = std::nullopt,
        },
    };
  }

  if (request.method.empty()) {
    return blazeclaw::gateway::protocol::ResponseFrame{
        .id = request.id,
        .ok = false,
        .payloadJson = std::nullopt,
        .error = blazeclaw::gateway::protocol::ErrorShape{
            .code = "invalid_method",
            .message = "Gateway method must not be empty.",
            .detailsJson = std::nullopt,
            .retryable = false,
            .retryAfterMs = std::nullopt,
        },
    };
  }

  return m_gatewayHost.RouteRequest(request);
}

bool ServiceManager::PumpGatewayNetworkOnce(std::string& error) {
  if (!m_running) {
    error = "service manager is not running";
    return false;
  }

  return m_gatewayHost.PumpNetworkOnce(error);
}

} // namespace blazeclaw::core
