#include "pch.h"
#include "GatewayAgentRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

GatewayAgentRegistry::GatewayAgentRegistry() {
  const AgentEntry defaultAgent = BuildDefaultAgent();
  m_agents.insert_or_assign(defaultAgent.id, defaultAgent);
}

std::vector<AgentEntry> GatewayAgentRegistry::List() const {
  std::vector<AgentEntry> output;
  output.reserve(m_agents.size());

  for (const auto& [_, agent] : m_agents) {
    output.push_back(agent);
  }

  std::sort(output.begin(), output.end(), [](const AgentEntry& left, const AgentEntry& right) {
    return left.id < right.id;
  });

  return output;
}

AgentEntry GatewayAgentRegistry::Get(const std::string& requestedId) const {
  const std::string id = NormalizeAgentId(requestedId);
  const auto it = m_agents.find(id);
  if (it != m_agents.end()) {
    return it->second;
  }

  return BuildDefaultAgent();
}

AgentEntry GatewayAgentRegistry::Activate(const std::string& requestedId) {
  const std::string id = NormalizeAgentId(requestedId);

  for (auto& [_, agent] : m_agents) {
    agent.active = false;
  }

  AgentEntry activated = Get(id);
  activated.active = true;
  m_agents.insert_or_assign(activated.id, activated);
  return activated;
}

std::string GatewayAgentRegistry::NormalizeAgentId(const std::string& value) {
  if (value.empty()) {
    return "default";
  }

  return value;
}

AgentEntry GatewayAgentRegistry::BuildDefaultAgent() {
  return AgentEntry{
      .id = "default",
      .name = "Default Agent",
      .active = true,
  };
}

} // namespace blazeclaw::gateway
