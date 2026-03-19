#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"
#include "FeatureRegistry.h"
#include "AgentsCatalogService.h"
#include "AgentsWorkspaceService.h"
#include "AcpSpawnService.h"
#include "PiEmbeddedService.h"
#include "SubagentRegistryService.h"
#include "SkillsCommandService.h"
#include "SkillsCatalogService.h"
#include "SkillsEnvOverrideService.h"
#include "SkillsEligibilityService.h"
#include "SkillsInstallService.h"
#include "SkillsPromptService.h"
#include "SkillSecurityScanService.h"
#include "SkillsSyncService.h"
#include "SkillsWatchService.h"

namespace blazeclaw::core {

class ServiceManager {
public:
  ServiceManager();

  bool Start(const blazeclaw::config::AppConfig& config);
  void Stop();

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] const FeatureRegistry& Registry() const noexcept;
  [[nodiscard]] const AgentScopeSnapshot& AgentsScope() const noexcept;
  [[nodiscard]] const AgentsWorkspaceSnapshot& AgentsWorkspace() const noexcept;
  [[nodiscard]] const SubagentRegistrySnapshot& SubagentRegistry() const noexcept;
  [[nodiscard]] const AcpSpawnDecision& LastAcpDecision() const noexcept;
  [[nodiscard]] std::size_t ActiveEmbeddedRuns() const noexcept;
  [[nodiscard]] const SkillsCatalogSnapshot& SkillsCatalog() const noexcept;
  [[nodiscard]] const SkillsEligibilitySnapshot& SkillsEligibility() const noexcept;
  [[nodiscard]] const SkillsPromptSnapshot& SkillsPrompt() const noexcept;
  [[nodiscard]] std::string InvokeGatewayMethod(
      const std::string& method,
      const std::optional<std::string>& paramsJson = std::nullopt) const;

private:
  [[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayState BuildGatewaySkillsState() const;
  void RefreshSkillsState(
      const blazeclaw::config::AppConfig& config,
      bool forceRefresh,
      const std::wstring& reason);

  bool m_running = false;
  blazeclaw::config::AppConfig m_activeConfig;
  FeatureRegistry m_registry;
  AgentsCatalogService m_agentsCatalogService;
  AgentScopeSnapshot m_agentsScope;
  AgentsWorkspaceService m_agentsWorkspaceService;
  AgentsWorkspaceSnapshot m_agentsWorkspace;
  SubagentRegistryService m_subagentRegistryService;
  SubagentRegistrySnapshot m_subagentRegistry;
  AcpSpawnService m_acpSpawnService;
  AcpSpawnDecision m_lastAcpDecision;
  PiEmbeddedService m_piEmbeddedService;
  SkillsCatalogService m_skillsCatalogService;
  SkillsCatalogSnapshot m_skillsCatalog;
  SkillsEligibilityService m_skillsEligibilityService;
  SkillsEligibilitySnapshot m_skillsEligibility;
  SkillsPromptService m_skillsPromptService;
  SkillsPromptSnapshot m_skillsPrompt;
  SkillsCommandService m_skillsCommandService;
  SkillsCommandSnapshot m_skillsCommands;
  SkillsSyncService m_skillsSyncService;
  SkillsSyncSnapshot m_skillsSync;
  SkillsEnvOverrideService m_skillsEnvOverrideService;
  SkillsEnvOverrideSnapshot m_skillsEnvOverrides;
  SkillsInstallService m_skillsInstallService;
  SkillsInstallSnapshot m_skillsInstall;
  SkillSecurityScanService m_skillSecurityScanService;
  SkillSecurityScanSnapshot m_skillSecurityScan;
  SkillsWatchService m_skillsWatchService;
  SkillsWatchSnapshot m_skillsWatch;
  blazeclaw::gateway::GatewayHost m_gatewayHost;
};

} // namespace blazeclaw::core
