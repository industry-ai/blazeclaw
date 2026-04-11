#include "pch.h"
#include "TransportRecipientRegistry.h"

namespace blazeclaw::gateway {

	void TransportRecipientRegistry::RegisterRecipient(
		const std::string& runId,
		const std::string& sessionKey,
		const std::string& connectionId,
		const std::uint64_t nowMs) {
		if (runId.empty() || sessionKey.empty() || connectionId.empty()) {
			return;
		}

		auto& runState = m_recipientsByRun[runId];
		runState.sessionKey = sessionKey;
		runState.finalized = false;
		runState.finalizedAtMs = 0;
		runState.sequence = m_nextSequence++;
		runState.recipients.insert_or_assign(
			connectionId,
			RecipientState{
				.connectionId = connectionId,
				.lastSeenMs = nowMs,
			});

		m_runsBySession[sessionKey].insert(runId);
	}

	void TransportRecipientRegistry::RegisterLateJoin(
		const std::string& sessionKey,
		const std::string& connectionId,
		const std::uint64_t nowMs) {
		if (sessionKey.empty() || connectionId.empty()) {
			return;
		}

		const auto sessionIt = m_runsBySession.find(sessionKey);
		if (sessionIt == m_runsBySession.end()) {
			return;
		}

		for (const auto& runId : sessionIt->second) {
			auto runIt = m_recipientsByRun.find(runId);
			if (runIt == m_recipientsByRun.end()) {
				continue;
			}

			if (runIt->second.finalized) {
				continue;
			}

			runIt->second.recipients.insert_or_assign(
				connectionId,
				RecipientState{
					.connectionId = connectionId,
					.lastSeenMs = nowMs,
				});
			runIt->second.sequence = m_nextSequence++;
		}
	}

	void TransportRecipientRegistry::MarkRunFinalized(
		const std::string& runId,
		const std::uint64_t nowMs) {
		auto runIt = m_recipientsByRun.find(runId);
		if (runIt == m_recipientsByRun.end()) {
			return;
		}

		runIt->second.finalized = true;
		runIt->second.finalizedAtMs = nowMs;
		runIt->second.sequence = m_nextSequence++;
	}

	void TransportRecipientRegistry::PruneDisconnected(
		const std::string& connectionId) {
		if (connectionId.empty()) {
			return;
		}

		for (auto runIt = m_recipientsByRun.begin();
			runIt != m_recipientsByRun.end();) {
			const std::string runId = runIt->first;
			const std::string sessionKey = runIt->second.sessionKey;
			runIt->second.recipients.erase(connectionId);
			if (runIt->second.recipients.empty() && runIt->second.finalized) {
				runIt = m_recipientsByRun.erase(runIt);
				auto sessionIt = m_runsBySession.find(sessionKey);
				if (sessionIt != m_runsBySession.end()) {
					sessionIt->second.erase(runId);
					if (sessionIt->second.empty()) {
						m_runsBySession.erase(sessionIt);
					}
				}
				continue;
			}

			++runIt;
		}
	}

	void TransportRecipientRegistry::PruneExpired(const std::uint64_t nowMs) {
		for (auto runIt = m_recipientsByRun.begin();
			runIt != m_recipientsByRun.end();) {
			auto& runState = runIt->second;
			for (auto recipientIt = runState.recipients.begin();
				recipientIt != runState.recipients.end();) {
				if (nowMs > recipientIt->second.lastSeenMs &&
					nowMs - recipientIt->second.lastSeenMs > m_recipientTtlMs) {
					recipientIt = runState.recipients.erase(recipientIt);
					continue;
				}
				++recipientIt;
			}

			const bool finalizedExpired = runState.finalized &&
				nowMs > runState.finalizedAtMs &&
				nowMs - runState.finalizedAtMs > m_finalizedGraceMs;
			if (runState.recipients.empty() || finalizedExpired) {
				const std::string sessionKey = runState.sessionKey;
				const std::string runId = runIt->first;
				runIt = m_recipientsByRun.erase(runIt);
				auto sessionIt = m_runsBySession.find(sessionKey);
				if (sessionIt != m_runsBySession.end()) {
					sessionIt->second.erase(runId);
					if (sessionIt->second.empty()) {
						m_runsBySession.erase(sessionIt);
					}
				}
				continue;
			}

			++runIt;
		}
	}

	void TransportRecipientRegistry::PruneRun(const std::string& runId) {
		auto runIt = m_recipientsByRun.find(runId);
		if (runIt == m_recipientsByRun.end()) {
			return;
		}

		const std::string sessionKey = runIt->second.sessionKey;
		m_recipientsByRun.erase(runIt);
		auto sessionIt = m_runsBySession.find(sessionKey);
		if (sessionIt != m_runsBySession.end()) {
			sessionIt->second.erase(runId);
			if (sessionIt->second.empty()) {
				m_runsBySession.erase(sessionIt);
			}
		}
	}

	bool TransportRecipientRegistry::HasRecipients(
		const std::string& runId) const {
		const auto runIt = m_recipientsByRun.find(runId);
		return runIt != m_recipientsByRun.end() &&
			!runIt->second.recipients.empty();
	}

	std::unordered_set<std::string> TransportRecipientRegistry::RecipientsForRun(
		const std::string& runId) const {
		std::unordered_set<std::string> recipients;
		const auto runIt = m_recipientsByRun.find(runId);
		if (runIt == m_recipientsByRun.end()) {
			return recipients;
		}

		for (const auto& [connectionId, _] : runIt->second.recipients) {
			recipients.insert(connectionId);
		}

		return recipients;
	}

	TransportRecipientRegistry::Snapshot TransportRecipientRegistry::GetSnapshot() const {
		Snapshot snapshot;
		snapshot.runCount = m_recipientsByRun.size();
		snapshot.activeSessionCount = m_runsBySession.size();
		snapshot.lastSequence = m_nextSequence > 0 ? (m_nextSequence - 1) : 0;
		for (const auto& [_, runState] : m_recipientsByRun) {
			snapshot.recipientCount += runState.recipients.size();
			if (runState.finalized) {
				++snapshot.finalizedRunCount;
			}
		}

		return snapshot;
	}

} // namespace blazeclaw::gateway
