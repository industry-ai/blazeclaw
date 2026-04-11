#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace blazeclaw::gateway {

	class TransportRecipientRegistry {
	public:
		struct RecipientState {
			std::string connectionId;
			std::uint64_t lastSeenMs = 0;
		};

		struct Snapshot {
			std::size_t runCount = 0;
			std::size_t recipientCount = 0;
			std::size_t activeSessionCount = 0;
			std::size_t finalizedRunCount = 0;
			std::uint64_t lastSequence = 0;
		};

		void RegisterRecipient(
			const std::string& runId,
			const std::string& sessionKey,
			const std::string& connectionId,
			std::uint64_t nowMs);
		void RegisterLateJoin(
			const std::string& sessionKey,
			const std::string& connectionId,
			std::uint64_t nowMs);
		void MarkRunFinalized(
			const std::string& runId,
			std::uint64_t nowMs);
		void PruneDisconnected(const std::string& connectionId);
		void PruneExpired(std::uint64_t nowMs);
		void PruneRun(const std::string& runId);

		[[nodiscard]] bool HasRecipients(const std::string& runId) const;
		[[nodiscard]] std::unordered_set<std::string> RecipientsForRun(
			const std::string& runId) const;
		[[nodiscard]] Snapshot GetSnapshot() const;

	private:
		struct RunRecipients {
			std::string sessionKey;
			std::unordered_map<std::string, RecipientState> recipients;
			bool finalized = false;
			std::uint64_t finalizedAtMs = 0;
			std::uint64_t sequence = 0;
		};

		std::unordered_map<std::string, RunRecipients> m_recipientsByRun;
		std::unordered_map<std::string, std::unordered_set<std::string>>
			m_runsBySession;
		std::uint64_t m_nextSequence = 1;
		std::uint64_t m_recipientTtlMs = 15 * 60 * 1000;
		std::uint64_t m_finalizedGraceMs = 60 * 1000;
	};

} // namespace blazeclaw::gateway
