#include "pch.h"
#include "ThreadPoolRuntimeService.h"

#include <algorithm>

namespace blazeclaw::core {

	ThreadPoolRuntimeService::ThreadPoolRuntimeService() = default;

	ThreadPoolRuntimeService::~ThreadPoolRuntimeService() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopping = true;
		}
		m_queueCv.notify_all();

		for (auto& worker : m_workers) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}

	void ThreadPoolRuntimeService::Configure(const Config& config) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config.minThreads = (std::max)(std::uint32_t{ 1 }, config.minThreads);
		m_config.maxThreads = (std::max)(m_config.minThreads, config.maxThreads);
		m_config.queueCapacity = (std::max)(std::uint32_t{ 1 }, config.queueCapacity);
		m_config.dequeueTimeoutMs = (std::max)(std::uint32_t{ 1 }, config.dequeueTimeoutMs);
		EnsureWorkersLocked();
	}

	ThreadPoolRuntimeService::ExecuteResult
		ThreadPoolRuntimeService::EnqueueAndExecute(const ExecuteRequest& request) {
		ExecuteResult executeResult{};
		if (!request.task) {
			executeResult.accepted = false;
			executeResult.completed = true;
			executeResult.diagnosticCode = "task_missing_callable";
			return executeResult;
		}

		std::shared_ptr<TaskEnvelope> taskEnvelope = std::make_shared<TaskEnvelope>();
		taskEnvelope->metadata.taskId = NextTaskId();
		taskEnvelope->metadata.runId = request.runId;
		taskEnvelope->metadata.sessionKey = request.sessionKey;
		taskEnvelope->metadata.responderRunId = request.responderRunId;
		taskEnvelope->metadata.enqueueAtMs = CurrentEpochMs();
		taskEnvelope->metadata.executionState = "queued";
		taskEnvelope->task = request.task;
		taskEnvelope->future = taskEnvelope->promise.get_future().share();

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			EnsureWorkersLocked();
			if (m_queue.size() >= m_config.queueCapacity) {
				executeResult.accepted = false;
				executeResult.completed = true;
				executeResult.saturated = true;
				executeResult.diagnosticCode = "thread_pool_queue_saturated";
				executeResult.metadata = taskEnvelope->metadata;
				return executeResult;
			}

			m_tasksById.insert_or_assign(
				taskEnvelope->metadata.taskId,
				taskEnvelope);
			m_queue.push_back(taskEnvelope);
		}
		m_queueCv.notify_one();

		executeResult.accepted = true;
		executeResult.metadata = taskEnvelope->metadata;

		const auto waitStatus = taskEnvelope->future.wait_for(
			std::chrono::milliseconds((std::max)(
				std::uint32_t{ 1 },
				request.timeoutMs)));
		if (waitStatus != std::future_status::ready) {
			taskEnvelope->cancellationRequested = true;
			executeResult.completed = false;
			executeResult.timedOut = true;
			executeResult.cancelled = true;
			executeResult.diagnosticCode = "thread_pool_task_timeout";
			executeResult.metadata = taskEnvelope->metadata;
			return executeResult;
		}

		executeResult.completed = true;
		executeResult.runtimeResult = taskEnvelope->future.get();
		executeResult.cancelled = taskEnvelope->metadata.executionState == "cancelled";
		executeResult.metadata = taskEnvelope->metadata;
		executeResult.diagnosticCode = taskEnvelope->metadata.executionState;
		return executeResult;
	}

	bool ThreadPoolRuntimeService::CancelTask(const std::string& taskId) {
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_tasksById.find(taskId);
		if (it == m_tasksById.end()) {
			return false;
		}

		it->second->cancellationRequested = true;
		if (it->second->metadata.executionState == "queued") {
			it->second->metadata.executionState = "cancel_requested";
		}
		return true;
	}

	bool ThreadPoolRuntimeService::WaitForTaskDrain(
		const std::string& taskId,
		std::uint32_t timeoutMs) {
		std::shared_ptr<TaskEnvelope> task;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_tasksById.find(taskId);
			if (it == m_tasksById.end()) {
				return true;
			}
			task = it->second;
		}

		std::unique_lock<std::mutex> completionLock(task->completionMutex);
		if (task->completed.load()) {
			return true;
		}

		return task->completionCv.wait_for(
			completionLock,
			std::chrono::milliseconds((std::max)(timeoutMs, std::uint32_t{ 1 })),
			[&task]() {
				return task->completed.load();
			});
	}

	std::size_t ThreadPoolRuntimeService::QueueDepth() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	std::size_t ThreadPoolRuntimeService::ActiveTaskCount() const {
		return m_activeTaskCount.load();
	}

	std::uint64_t ThreadPoolRuntimeService::CurrentEpochMs() const {
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
			.count());
	}

	std::string ThreadPoolRuntimeService::NextTaskId() {
		const auto sequence = ++m_sequence;
		return std::string("chat-task-") + std::to_string(CurrentEpochMs()) +
			"-" +
			std::to_string(sequence);
	}

	void ThreadPoolRuntimeService::EnsureWorkersLocked() {
		if (m_stopping) {
			return;
		}

		const std::size_t desiredWorkers = static_cast<std::size_t>(m_config.maxThreads);
		if (m_workers.size() >= desiredWorkers) {
			return;
		}

		for (std::size_t index = m_workers.size(); index < desiredWorkers; ++index) {
			m_workers.emplace_back([this]() {
				WorkerLoop();
				});
		}
	}

	void ThreadPoolRuntimeService::WorkerLoop() {
		while (!m_stopping) {
			std::shared_ptr<TaskEnvelope> task;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if (m_queue.empty()) {
					m_queueCv.wait_for(
						lock,
						std::chrono::milliseconds(m_config.dequeueTimeoutMs),
						[this]() {
							return m_stopping || !m_queue.empty();
						});
				}

				if (m_stopping) {
					return;
				}
				if (m_queue.empty()) {
					continue;
				}

				task = m_queue.front();
				m_queue.pop_front();
			}

			if (!task) {
				continue;
			}

			if (task->cancellationRequested.load()) {
				FinalizeTask(
					task,
					blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
						.ok = false,
						.errorCode = "chat_runtime_cancelled",
						.errorMessage = "task cancelled before execution"
					},
					"cancelled");
				continue;
			}

			task->metadata.startAtMs = CurrentEpochMs();
			task->metadata.queueWaitMs =
				task->metadata.startAtMs > task->metadata.enqueueAtMs
				? task->metadata.startAtMs - task->metadata.enqueueAtMs
				: 0;
			task->metadata.executionState = "running";
			m_activeTaskCount.fetch_add(1);

			blazeclaw::gateway::GatewayHost::ChatRuntimeResult runtimeResult{};
			try {
				runtimeResult = task->task();
			}
			catch (const std::exception& ex) {
				runtimeResult.ok = false;
				runtimeResult.errorCode = "thread_pool_execution_exception";
				runtimeResult.errorMessage = ex.what();
			}
			catch (...) {
				runtimeResult.ok = false;
				runtimeResult.errorCode = "thread_pool_execution_exception";
				runtimeResult.errorMessage = "unknown exception in thread pool task";
			}
			m_activeTaskCount.fetch_sub(1);

			const std::string terminalState = task->cancellationRequested.load()
				? "cancelled"
				: (runtimeResult.ok ? "completed" : "failed");
			FinalizeTask(task, runtimeResult, terminalState);
		}
	}

	void ThreadPoolRuntimeService::FinalizeTask(
		const std::shared_ptr<TaskEnvelope>& task,
		const blazeclaw::gateway::GatewayHost::ChatRuntimeResult& result,
		const std::string& terminalState) {
		task->metadata.completedAtMs = CurrentEpochMs();
		task->metadata.runDurationMs =
			task->metadata.completedAtMs > task->metadata.startAtMs
			? task->metadata.completedAtMs - task->metadata.startAtMs
			: 0;
		task->metadata.executionState = terminalState;
		task->completed = true;
		try {
			task->promise.set_value(result);
		}
		catch (...) {
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_tasksById.erase(task->metadata.taskId);
		}

		{
			std::lock_guard<std::mutex> lock(task->completionMutex);
		}
		task->completionCv.notify_all();
	}

} // namespace blazeclaw::core
