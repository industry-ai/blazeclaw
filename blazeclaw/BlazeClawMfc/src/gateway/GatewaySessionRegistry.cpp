#include "pch.h"
#include "GatewaySessionRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

GatewaySessionRegistry::GatewaySessionRegistry() {
  const SessionEntry mainSession = BuildDefaultSession();
  m_sessions.insert_or_assign(mainSession.id, mainSession);
}

SessionEntry GatewaySessionRegistry::Create(const std::string& requestedId) {
  const std::string id = NormalizeSessionId(requestedId);

  SessionEntry created{
      .id = id,
      .scope = id == "main" ? "default" : "thread",
      .active = true,
  };

  m_sessions.insert_or_assign(id, created);
  return created;
}

SessionEntry GatewaySessionRegistry::Resolve(const std::string& requestedId) const {
  const std::string id = NormalizeSessionId(requestedId);
  const auto it = m_sessions.find(id);
  if (it != m_sessions.end()) {
    return it->second;
  }

  return BuildDefaultSession();
}

std::vector<SessionEntry> GatewaySessionRegistry::List() const {
  std::vector<SessionEntry> output;
  output.reserve(m_sessions.size());

  for (const auto& [_, session] : m_sessions) {
    output.push_back(session);
  }

  std::sort(output.begin(), output.end(), [](const SessionEntry& left, const SessionEntry& right) {
    return left.id < right.id;
  });

  return output;
}

SessionEntry GatewaySessionRegistry::Reset(const std::string& requestedId) {
  const std::string id = NormalizeSessionId(requestedId);
  SessionEntry reset{
      .id = id,
      .scope = id == "main" ? "default" : "thread",
      .active = true,
  };

  m_sessions.insert_or_assign(id, reset);
  return reset;
}

std::string GatewaySessionRegistry::NormalizeSessionId(const std::string& value) {
  if (value.empty()) {
    return "main";
  }

  return value;
}

SessionEntry GatewaySessionRegistry::BuildDefaultSession() {
  return SessionEntry{
      .id = "main",
      .scope = "default",
      .active = true,
  };
}

} // namespace blazeclaw::gateway
