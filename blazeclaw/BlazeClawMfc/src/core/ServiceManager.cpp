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
  if (!m_running) {
    return "service_not_running";
  }

  if (method.empty()) {
    return "invalid_method";
  }

  const blazeclaw::gateway::protocol::RequestFrame request{
      .id = "ui-probe",
      .method = method,
      .paramsJson = paramsJson,
  };

  const auto response = m_gatewayHost.RouteRequest(request);
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

} // namespace blazeclaw::core
