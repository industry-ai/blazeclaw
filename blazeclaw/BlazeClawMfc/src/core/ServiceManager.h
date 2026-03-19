#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"
#include "FeatureRegistry.h"
#include "SkillsCatalogService.h"

namespace blazeclaw::core {

class ServiceManager {
public:
  ServiceManager();

  bool Start(const blazeclaw::config::AppConfig& config);
  void Stop();

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] const FeatureRegistry& Registry() const noexcept;
  [[nodiscard]] const SkillsCatalogSnapshot& SkillsCatalog() const noexcept;
  [[nodiscard]] std::string InvokeGatewayMethod(
      const std::string& method,
      const std::optional<std::string>& paramsJson = std::nullopt) const;

private:
  bool m_running = false;
  FeatureRegistry m_registry;
  SkillsCatalogService m_skillsCatalogService;
  SkillsCatalogSnapshot m_skillsCatalog;
  blazeclaw::gateway::GatewayHost m_gatewayHost;
};

} // namespace blazeclaw::core
