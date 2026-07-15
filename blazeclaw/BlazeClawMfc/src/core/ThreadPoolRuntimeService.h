#pragma once

#include "../gateway/GatewayHost.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace blazeclaw::core {

	class ThreadPoolRuntimeService {
	public:
		struct Config {
			std::uint32_t minThreads = 1;
			std::uint32_t maxThreads = 2;
			std::uint32_t queueCapacity = 128;
			std::uint32_t dequeueTimeoutMs = 50;
		};

		struct TaskMetadata {
			std::string taskId;
			std::string runId;
			std::string sessionKey;
			std::string responderRunId;
			std::uint64_t enqueueAtMs = 0;
			std::uint64_t startAtMs = 0;
			std::uint64_t completedAtMs = 0;
			std::uint64_t queueWaitMs = 0;
			std::uint64_t runDurationMs = 0;
			std::string executionState = "queued";
		};

		struct ExecuteRequest {
			std::string runId;
			std::string sessionKey;
			std::string responderRunId;
			std::uint32_t timeoutMs = 90000;
			std::function<blazeclaw::gateway::GatewayHost::ChatRuntimeResult()> task;
		};

		struct ExecuteResult {
			bool accepted = false;
			bool completed = false;
			bool timedOut = false;
			bool cancelled = false;
			bool saturated = false;
			std::string diagnosticCode;
			TaskMetadata metadata;
			std::optional<blazeclaw::gateway::GatewayHost::ChatRuntimeResult> runtimeResult;
		};

		ThreadPoolRuntimeService();
		~ThreadPoolRuntimeService();

		ThreadPoolRuntimeService(const ThreadPoolRuntimeService&) = delete;
		ThreadPoolRuntimeService& operator=(const ThreadPoolRuntimeService&) = delete;

		void Configure(const Config& config);

		[[nodiscard]] ExecuteResult EnqueueAndExecute(const ExecuteRequest& request);
		[[nodiscard]] bool CancelTask(const std::string& taskId);
		[[nodiscard]] bool WaitForTaskDrain(
			const std::string& taskId,
			std::uint32_t timeoutMs);

		[[nodiscard]] std::size_t QueueDepth() const;
		[[nodiscard]] std::size_t ActiveTaskCount() const;

	private:
		struct TaskEnvelope {
			TaskMetadata metadata;
			std::function<blazeclaw::gateway::GatewayHost::ChatRuntimeResult()> task;
			std::promise<blazeclaw::gateway::GatewayHost::ChatRuntimeResult> promise;
			std::shared_future<blazeclaw::gateway::GatewayHost::ChatRuntimeResult> future;
			std::atomic<bool> cancellationRequested = false;
			std::atomic<bool> completed = false;
			std::mutex completionMutex;
			std::condition_variable completionCv;
		};

		[[nodiscard]] std::uint64_t CurrentEpochMs() const;
		[[nodiscard]] std::string NextTaskId();
		void EnsureWorkersLocked();
		void WorkerLoop();
		void FinalizeTask(
			const std::shared_ptr<TaskEnvelope>& task,
			const blazeclaw::gateway::GatewayHost::ChatRuntimeResult& result,
			const std::string& terminalState);

		mutable std::mutex m_mutex;
		std::condition_variable m_queueCv;
		Config m_config{};
		std::deque<std::shared_ptr<TaskEnvelope>> m_queue;
		std::unordered_map<std::string, std::shared_ptr<TaskEnvelope>> m_tasksById;
		std::vector<std::thread> m_workers;
		std::atomic<bool> m_stopping = false;
		std::atomic<std::uint64_t> m_sequence = 0;
		std::atomic<std::size_t> m_activeTaskCount = 0;
	};

} // namespace blazeclaw::core
