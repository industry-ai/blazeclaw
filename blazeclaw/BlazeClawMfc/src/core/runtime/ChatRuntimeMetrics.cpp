#include "pch.h"
#include "ChatRuntimeMetrics.h"

namespace blazeclaw::core {

	void ChatRuntimeMetrics::OnEnqueued()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.enqueuedRuns;
	}

	void ChatRuntimeMetrics::OnCompleted()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.completedRuns;
	}

	void ChatRuntimeMetrics::OnFailed()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.failedRuns;
	}

	void ChatRuntimeMetrics::OnCancelled()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.cancelledRuns;
	}

	void ChatRuntimeMetrics::OnTimedOut()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.timedOutRuns;
	}

	void ChatRuntimeMetrics::OnWorkerUnavailable()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.workerUnavailableRuns;
	}

	void ChatRuntimeMetrics::OnQueueTimeout()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.queueTimeouts;
	}

	void ChatRuntimeMetrics::OnAbortRequested()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.abortRequests;
	}

	ChatRuntimeMetricsSnapshot ChatRuntimeMetrics::Snapshot() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

} // namespace blazeclaw::core
