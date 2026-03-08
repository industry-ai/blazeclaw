#include "pch.h"
#include "ServiceManager.h"

namespace blazeclaw::core {

ServiceManager::ServiceManager() = default;

bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
  if (m_running) {
    return true;
  }

  const bool gatewayStarted = m_gatewayHost.Start(config.gateway);
  m_running = gatewayStarted;
  return m_running;
}

void ServiceManager::Stop() {
  m_gatewayHost.Stop();
  m_running = false;
}

bool ServiceManager::IsRunning() const noexcept {
  return m_running;
}

const FeatureRegistry& ServiceManager::Registry() const noexcept {
  return m_registry;
}

} // namespace blazeclaw::core
