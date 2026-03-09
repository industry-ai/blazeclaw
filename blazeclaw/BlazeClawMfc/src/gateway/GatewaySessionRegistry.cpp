#include "pch.h"
#include "GatewaySessionRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

GatewaySessionRegistry::GatewaySessionRegistry() {
  const SessionEntry mainSession = BuildDefaultSession();
  m_sessions.insert_or_assign(mainSession.id, mainSession);
}

SessionEntry GatewaySessionRegistry::Create(
    const std::string& requestedId,
    const std::optional<std::string>& requestedScope,
    std::optional<bool> requestedActive) {
  const std::string id = NormalizeSessionId(requestedId);

  SessionEntry created{
      .id = id,
      .scope = ResolveScope(id, requestedScope),
      .active = requestedActive.value_or(true),
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

SessionEntry GatewaySessionRegistry::Reset(
	const std::string& requestedId,
	const std::optional<std::string>& requestedScope,
	std::optional<bool> requestedActive) {
	const std::string id = NormalizeSessionId(requestedId);
	SessionEntry reset{
		.id = id,
		.scope = ResolveScope(id, requestedScope),
		.active = requestedActive.value_or(true),
	};

	m_sessions.insert_or_assign(id, reset);
	return reset;
}

bool GatewaySessionRegistry::Delete(const std::string& requestedId, SessionEntry& removedSession) {
	const std::string id = NormalizeSessionId(requestedId);
	if (id == "main") {
		removedSession = Resolve(id);
		return false;
	}

	const auto it = m_sessions.find(id);
	if (it == m_sessions.end()) {
		removedSession = SessionEntry{
			.id = id,
			.scope = ResolveScope(id, std::nullopt),
			.active = false,
		};
		return false;
	}

	removedSession = it->second;
	removedSession.active = false;
	m_sessions.erase(it);
	return true;
}

std::size_t GatewaySessionRegistry::CountCompactCandidates() const {
  std::size_t count = 0;
  for (const auto& [id, session] : m_sessions) {
    if (id != "main" && !session.active) {
      ++count;
    }
  }

  return count;
}

std::size_t GatewaySessionRegistry::CompactInactive() {
  std::size_t compacted = 0;
  for (auto it = m_sessions.begin(); it != m_sessions.end();) {
    if (it->first != "main" && !it->second.active) {
      it = m_sessions.erase(it);
      ++compacted;
      continue;
    }

    ++it;
  }

  return compacted;
}

std::string GatewaySessionRegistry::ResolveScope(
    const std::string& sessionId,
    const std::optional<std::string>& requestedScope) {
  if (requestedScope.has_value() && !requestedScope.value().empty()) {
    return requestedScope.value();
  }

  return sessionId == "main" ? "default" : "thread";
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
