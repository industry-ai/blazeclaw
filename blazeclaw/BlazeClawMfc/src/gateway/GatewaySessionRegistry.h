#pragma once

#include "../app/pch.h"

#include <filesystem>

namespace blazeclaw::gateway {

	struct SessionEntry {
		std::string id;
		std::string scope;
		bool active = true;
	};

	class GatewaySessionRegistry {
	public:
		GatewaySessionRegistry();

		SessionEntry Create(
			const std::string& requestedId,
			const std::optional<std::string>& requestedScope = std::nullopt,
			std::optional<bool> requestedActive = std::nullopt);
		SessionEntry Resolve(const std::string& requestedId) const;
		std::vector<SessionEntry> List() const;
		SessionEntry Reset(
			const std::string& requestedId,
			const std::optional<std::string>& requestedScope = std::nullopt,
			std::optional<bool> requestedActive = std::nullopt);
		bool Delete(const std::string& requestedId, SessionEntry& removedSession);
		std::size_t CountCompactCandidates() const;
		std::size_t CompactInactive();
		SessionEntry Patch(
			const std::string& requestedId,
			const std::optional<std::string>& requestedScope = std::nullopt,
			std::optional<bool> requestedActive = std::nullopt);

	private:
		static std::string NormalizeSessionId(const std::string& value);
		static std::string ResolveScope(const std::string& sessionId, const std::optional<std::string>& requestedScope);
		static SessionEntry BuildDefaultSession();
		static std::filesystem::path PersistencePath();

		void LoadPersistedSessions();
		void PersistSessions() const;

		std::unordered_map<std::string, SessionEntry> m_sessions;
	};

} // namespace blazeclaw::gateway
