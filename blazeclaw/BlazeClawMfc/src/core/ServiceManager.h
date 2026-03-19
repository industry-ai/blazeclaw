#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"
#include "FeatureRegistry.h"
#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"
#include "SkillsPromptService.h"

namespace blazeclaw::core {

class ServiceManager {
public:
  ServiceManager();

  bool Start(const blazeclaw::config::AppConfig& config);
  void Stop();

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] const FeatureRegistry& Registry() const noexcept;
  [[nodiscard]] const SkillsCatalogSnapshot& SkillsCatalog() const noexcept;
  [[nodiscard]] const SkillsEligibilitySnapshot& SkillsEligibility() const noexcept;
  [[nodiscard]] const SkillsPromptSnapshot& SkillsPrompt() const noexcept;
  [[nodiscard]] std::string InvokeGatewayMethod(
      const std::string& method,
      const std::optional<std::string>& paramsJson = std::nullopt) const;

private:
  bool m_running = false;
  FeatureRegistry m_registry;
  SkillsCatalogService m_skillsCatalogService;
  SkillsCatalogSnapshot m_skillsCatalog;
  SkillsEligibilityService m_skillsEligibilityService;
  SkillsEligibilitySnapshot m_skillsEligibility;
  SkillsPromptService m_skillsPromptService;
  SkillsPromptSnapshot m_skillsPrompt;
  blazeclaw::gateway::GatewayHost m_gatewayHost;
};

} // namespace blazeclaw::core
