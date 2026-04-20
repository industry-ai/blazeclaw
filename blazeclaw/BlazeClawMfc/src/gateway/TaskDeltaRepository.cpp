#include "pch.h"
#include "TaskDeltaRepository.h"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace blazeclaw::gateway {

	namespace {

		std::uint64_t NowEpochMs() {
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
		}

		std::uint64_t MaxTimestampFromEntries(const std::vector<TaskDeltaEntry>& entries) {
			std::uint64_t m = 0;
			for (const auto& e : entries) {
				m = (std::max)(m, e.startedAtMs);
				m = (std::max)(m, e.completedAtMs);
			}
			return m;
		}

	} // namespace

	TaskDeltaRepository::TaskDeltaRepository(Store& backingStore)
		: m_backingStore(backingStore) {}

	bool TaskDeltaRepository::Upsert(
		const std::string& runId,
		const std::vector<TaskDeltaEntry>& entries,
		std::optional<std::uint64_t> lastActivityMs) {
		if (runId.empty()) {
			return false;
		}

		m_backingStore.insert_or_assign(runId, entries);
		std::uint64_t activity = 0;
		if (lastActivityMs.has_value()) {
			activity = *lastActivityMs;
			if (activity == 0) {
				activity = 1;
			}
		}
		else {
			activity = (std::max)(NowEpochMs(), MaxTimestampFromEntries(entries));
			if (activity == 0) {
				activity = NowEpochMs();
			}
		}
		m_lastActivityMs[runId] = activity;
		return true;
	}

	std::optional<std::vector<TaskDeltaEntry>> TaskDeltaRepository::Get(
		const std::string& runId,
		bool touchRecency) const {
		if (runId.empty()) {
			return std::nullopt;
		}

		const auto it = m_backingStore.find(runId);
		if (it == m_backingStore.end()) {
			return std::nullopt;
		}

		if (touchRecency) {
			m_lastActivityMs[runId] = NowEpochMs();
		}

		return it->second;
	}

	bool TaskDeltaRepository::Clear(const std::string& runId) {
		if (runId.empty()) {
			return false;
		}

		const auto it = m_backingStore.find(runId);
		if (it == m_backingStore.end()) {
			return false;
		}

		m_backingStore.erase(it);
		m_lastActivityMs.erase(runId);
		return true;
	}

	void TaskDeltaRepository::ClearAll() {
		m_backingStore.clear();
		m_lastActivityMs.clear();
	}

	std::size_t TaskDeltaRepository::EnforceRetentionLimit(std::size_t maxRuns) {
		std::size_t evicted = 0;
		while (m_backingStore.size() > maxRuns && !m_backingStore.empty()) {
			std::string victim;
			std::uint64_t minTs = UINT64_MAX;
			for (const auto& kv : m_backingStore) {
				const auto rIt = m_lastActivityMs.find(kv.first);
				const std::uint64_t ts =
					rIt != m_lastActivityMs.end() ? rIt->second : 0;
				if (ts < minTs || (ts == minTs && (victim.empty() || kv.first < victim))) {
					minTs = ts;
					victim = kv.first;
				}
			}
			if (victim.empty()) {
				break;
			}
			(void)Clear(victim);
			++evicted;
		}
		return evicted;
	}

	std::size_t TaskDeltaRepository::Size() const noexcept {
		return m_backingStore.size();
	}

	const TaskDeltaRepository::Store& TaskDeltaRepository::Snapshot() const noexcept {
		return m_backingStore;
	}

} // namespace blazeclaw::gateway
