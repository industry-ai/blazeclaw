#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

struct AgentEntry {
  std::string id;
  std::string name;
  bool active = false;
};

class GatewayAgentRegistry {
public:
  GatewayAgentRegistry();

  std::vector<AgentEntry> List() const;
  AgentEntry Get(const std::string& requestedId) const;
  AgentEntry Activate(const std::string& requestedId);

private:
  static std::string NormalizeAgentId(const std::string& value);
  static AgentEntry BuildDefaultAgent();

  std::unordered_map<std::string, AgentEntry> m_agents;
};

} // namespace blazeclaw::gateway
