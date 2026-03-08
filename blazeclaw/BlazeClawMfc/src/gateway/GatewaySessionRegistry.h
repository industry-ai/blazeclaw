#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

struct SessionEntry {
  std::string id;
  std::string scope;
  bool active = true;
};

class GatewaySessionRegistry {
public:
  GatewaySessionRegistry();

  SessionEntry Create(const std::string& requestedId);
  SessionEntry Resolve(const std::string& requestedId) const;
  std::vector<SessionEntry> List() const;
  SessionEntry Reset(const std::string& requestedId);

private:
  static std::string NormalizeSessionId(const std::string& value);
  static SessionEntry BuildDefaultSession();

  std::unordered_map<std::string, SessionEntry> m_sessions;
};

} // namespace blazeclaw::gateway
