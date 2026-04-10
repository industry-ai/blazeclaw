#include "pch.h"
#include "TaskDeltaRepository.h"

namespace blazeclaw::gateway {

	TaskDeltaRepository::TaskDeltaRepository(Store& backingStore)
		: m_backingStore(backingStore) {}

	bool TaskDeltaRepository::Upsert(
		const std::string& runId,
		const std::vector<TaskDeltaEntry>& entries) {
		if (runId.empty()) {
			return false;
		}

		m_backingStore.insert_or_assign(runId, entries);
		return true;
	}

	std::optional<std::vector<TaskDeltaEntry>> TaskDeltaRepository::Get(
		const std::string& runId) const {
		if (runId.empty()) {
			return std::nullopt;
		}

		const auto it = m_backingStore.find(runId);
		if (it == m_backingStore.end()) {
			return std::nullopt;
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
		return true;
	}

	void TaskDeltaRepository::ClearAll() {
		m_backingStore.clear();
	}

	std::size_t TaskDeltaRepository::Size() const noexcept {
		return m_backingStore.size();
	}

	const TaskDeltaRepository::Store& TaskDeltaRepository::Snapshot() const noexcept {
		return m_backingStore;
	}

} // namespace blazeclaw::gateway
