#include "pch.h"
#include "FeatureRegistry.h"

namespace blazeclaw::core {

FeatureRegistry::FeatureRegistry() {
  m_features = {
      {L"gateway-control-plane", FeatureState::InProgress},
      {L"session-routing", FeatureState::Planned},
      {L"agent-runtime", FeatureState::Planned},
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
