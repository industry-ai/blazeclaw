#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"
#include "FeatureRegistry.h"

namespace blazeclaw::core {

class ServiceManager {
public:
  ServiceManager();

  bool Start(const blazeclaw::config::AppConfig& config);
  void Stop();

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] const FeatureRegistry& Registry() const noexcept;

private:
  bool m_running = false;
  FeatureRegistry m_registry;
  blazeclaw::gateway::GatewayHost m_gatewayHost;
};

} // namespace blazeclaw::core
