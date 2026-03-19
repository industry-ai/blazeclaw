#include "pch.h"
#include "FeatureRegistry.h"

namespace blazeclaw::core {

FeatureRegistry::FeatureRegistry() {
  m_features = {
      {L"gateway-control-plane", FeatureState::InProgress},
      {L"session-routing", FeatureState::Planned},
      {L"agent-runtime", FeatureState::Planned},
      {L"agents-a1-config-foundation", FeatureState::Implemented},
      {L"agents-a1-scope-resolution", FeatureState::Implemented},
      {L"agents-a1-workspace-onboarding", FeatureState::Implemented},
      {L"agents-a2-run-wait-lifecycle", FeatureState::Implemented},
      {L"agents-a2-crud-files-idempotency", FeatureState::Implemented},
      {L"agents-a2-files-path-guardrails", FeatureState::Implemented},
      {L"skills-config-foundation", FeatureState::InProgress},
      {L"skills-catalog-discovery", FeatureState::Planned},
      {L"skills-eligibility-filtering", FeatureState::Implemented},
      {L"skills-prompt-snapshots", FeatureState::Implemented},
      {L"skills-watch-refresh", FeatureState::Implemented},
      {L"skills-sandbox-sync", FeatureState::Implemented},
      {L"skills-installer-scanner", FeatureState::Implemented},
      {L"skills-operator-surface", FeatureState::Implemented},
      {L"streaming-responses", FeatureState::Planned},
      {L"whatsapp-channel", FeatureState::Planned},
      {L"telegram-channel", FeatureState::Planned},
      {L"slack-channel", FeatureState::Planned},
      {L"discord-channel", FeatureState::Planned},
      {L"canvas-host", FeatureState::Planned},
      {L"voice-nodes", FeatureState::Planned},
  };
}

const std::vector<FeatureEntry>& FeatureRegistry::Features() const noexcept {
  return m_features;
}

bool FeatureRegistry::IsImplemented(const std::wstring& name) const {
  for (const auto& feature : m_features) {
    if (feature.name == name) {
      return feature.state == FeatureState::Implemented;
    }
  }

  return false;
}

} // namespace blazeclaw::core
