#pragma once

#include <cstdint>
#include <mutex>

namespace blazeclaw::core {

	struct ChatRuntimeMetricsSnapshot {
		std::uint64_t enqueuedRuns = 0;
		std::uint64_t completedRuns = 0;
		std::uint64_t failedRuns = 0;
		std::uint64_t cancelledRuns = 0;
		std::uint64_t timedOutRuns = 0;
		std::uint64_t workerUnavailableRuns = 0;
		std::uint64_t queueTimeouts = 0;
		std::uint64_t abortRequests = 0;
	};

	class ChatRuntimeMetrics {
	public:
		void OnEnqueued();
		void OnCompleted();
		void OnFailed();
		void OnCancelled();
		void OnTimedOut();
		void OnWorkerUnavailable();
		void OnQueueTimeout();
		void OnAbortRequested();

		[[nodiscard]] ChatRuntimeMetricsSnapshot Snapshot() const;

	private:
		mutable std::mutex m_mutex;
		ChatRuntimeMetricsSnapshot m_snapshot;
	};

} // namespace blazeclaw::core
