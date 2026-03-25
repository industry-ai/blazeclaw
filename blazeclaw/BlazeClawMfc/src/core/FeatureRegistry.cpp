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
      {L"agents-a3-subagent-spawn-policy", FeatureState::Implemented},
      {L"agents-a3-subagent-registry-persistence", FeatureState::Implemented},
      {L"agents-a3-subagent-announce-orphan-lifecycle", FeatureState::Implemented},
      {L"agents-a4-acp-spawn-policy", FeatureState::Implemented},
      {L"agents-a4-embedded-runner-bridge", FeatureState::Implemented},
      {L"agents-a4-runtime-mode-sandbox-compat", FeatureState::Implemented},
      {L"agents-a5-tool-catalog-policy", FeatureState::Implemented},
      {L"agents-a5-owner-only-guardrails", FeatureState::Implemented},
      {L"agents-a5-shell-process-runtime", FeatureState::Implemented},
      {L"agents-a6-model-routing-selection", FeatureState::Implemented},
      {L"agents-a6-failover-accounting", FeatureState::Implemented},
      {L"agents-a6-auth-profile-lifecycle", FeatureState::Implemented},
      {L"agents-a7-sandbox-policy-runtime", FeatureState::Implemented},
      {L"agents-a7-transcript-safety-guards", FeatureState::Implemented},
      {L"agents-a7-redaction-write-lock", FeatureState::Implemented},
      {L"skills-config-foundation", FeatureState::InProgress},
      {L"skills-catalog-discovery", FeatureState::Planned},
      {L"hooks-catalog-loader", FeatureState::Implemented},
      {L"hooks-event-emission", FeatureState::Implemented},
      {L"hooks-execution-engine", FeatureState::Implemented},
      {L"hooks-self-evolving-integration", FeatureState::Implemented},
      {L"hooks-reminder-policy-controls", FeatureState::Implemented},
      {L"hooks-reminder-transition-telemetry", FeatureState::Implemented},
      {L"hooks-runtime-additional-packages", FeatureState::Implemented},
      {L"hooks-governance-policy-enforcement", FeatureState::Implemented},
      {L"hooks-policy-drift-detection", FeatureState::Implemented},
      {L"hooks-governance-reporting-pipeline", FeatureState::Implemented},
      {L"hooks-governance-centralized-observability", FeatureState::Implemented},
      {L"skills-eligibility-filtering", FeatureState::Implemented},
      {L"skills-prompt-snapshots", FeatureState::Implemented},
      {L"skills-watch-refresh", FeatureState::Implemented},
      {L"skills-sandbox-sync", FeatureState::Implemented},
      {L"skills-installer-scanner", FeatureState::Implemented},
      {L"skills-operator-surface", FeatureState::Implemented},
      {L"embeddings-config-foundation", FeatureState::Implemented},
      {L"embeddings-provider-onnx", FeatureState::Implemented},
      {L"embeddings-gateway-surface", FeatureState::Planned},
      {L"embeddings-retrieval-memory", FeatureState::Implemented},
      {L"embeddings-chat-retrieval-context", FeatureState::Implemented},
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
