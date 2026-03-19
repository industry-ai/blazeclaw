#include "pch.h"
#include "ServiceManager.h"

#include "../gateway/GatewayProtocolModels.h"

#include <filesystem>

namespace blazeclaw::core {

ServiceManager::ServiceManager() = default;

bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
  if (m_running) {
    return true;
  }

  m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
      std::filesystem::current_path(),
      config);

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

const SkillsCatalogSnapshot& ServiceManager::SkillsCatalog() const noexcept {
  return m_skillsCatalog;
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
