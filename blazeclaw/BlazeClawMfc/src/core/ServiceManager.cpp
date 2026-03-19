#include "pch.h"
#include "ServiceManager.h"

#include "../gateway/GatewayProtocolModels.h"

#include <filesystem>

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

bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
  if (m_running) {
    return true;
  }

  m_skillsCatalog = m_skillsCatalogService.LoadCatalog(
      std::filesystem::current_path(),
      config);

  std::wstring fixtureError;
  const std::vector<std::filesystem::path> fixtureCandidates = {
      std::filesystem::current_path() / L"blazeclaw" / L"fixtures" / L"skills-catalog",
      std::filesystem::current_path() / L"fixtures" / L"skills-catalog",
  };

  for (const auto& candidate : fixtureCandidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec) || ec) {
      continue;
    }

    if (!m_skillsCatalogService.ValidateFixtureScenarios(candidate, fixtureError)) {
      m_skillsCatalog.diagnostics.warnings.push_back(
          L"skills-catalog fixture validation failed: " + fixtureError);
    }
    break;
  }

  blazeclaw::gateway::SkillsCatalogGatewayState gatewaySkillsState;
  gatewaySkillsState.entries.reserve(m_skillsCatalog.entries.size());
  for (const auto& entry : m_skillsCatalog.entries) {
    gatewaySkillsState.entries.push_back(
        blazeclaw::gateway::SkillsCatalogGatewayEntry{
            .name = ToNarrow(entry.skillName),
            .description = ToNarrow(entry.description),
            .source = ToNarrow(SkillsCatalogService::SourceKindLabel(entry.sourceKind)),
            .precedence = entry.precedence,
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
  gatewaySkillsState.warningCount =
      m_skillsCatalog.diagnostics.warnings.size();
  m_gatewayHost.SetSkillsCatalogState(std::move(gatewaySkillsState));

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
