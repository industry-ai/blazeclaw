#pragma once

#include "../../gateway/GatewayHost.h"
#include "ChatRuntimeMetrics.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace blazeclaw::core {

	class CChatRuntime {
	public:
		enum class JobLifecycleStatus {
			Queued,
			Started,
			Delta,
			Completed,
			Failed,
			Cancelled,
			TimedOut,
		};

		struct Config {
			std::size_t queueCapacity = 64;
			std::uint64_t queueWaitTimeoutMs = 15000;
			std::uint64_t executionTimeoutMs = 120000;
			bool asyncQueueEnabled = true;
			std::string errorQueueFull = "chat_runtime_queue_full";
			std::string errorCancelled = "chat_runtime_cancelled";
			std::string errorTimedOut = "chat_runtime_timed_out";
			std::string errorWorkerUnavailable = "chat_runtime_worker_unavailable";
		};

		struct Dependencies {
			std::function<bool(const std::string&, const std::string&)> isRunCancelled;
			std::function<void(const std::string&, const std::string&)> onQueueTimeout;
			std::function<void(const std::string&, const std::string&)> onQueueTimeoutCleanup;
			std::function<void(const std::string&, const std::string&, bool)> onAbort;
			std::function<void(const std::string&, const std::string&)> onWorkerCompleted;
			std::function<bool(const std::string&)> cancelActiveRuntime;
		};

		struct RuntimeExecutionRequest {
			blazeclaw::gateway::GatewayHost::ChatRuntimeRequest request;
			std::string sessionId;
			std::string runtimeMessage;
			std::string provider;
			std::string model;
			std::function<blazeclaw::gateway::GatewayHost::ChatRuntimeResult()> execute;
		};

		struct ChatRuntimeRunState {
			std::string runId;
			std::string sessionId;
			std::string provider;
			std::string model;
			std::uint64_t enqueuedAtMs = 0;
			std::uint64_t startedAtMs = 0;
			std::uint64_t completedAtMs = 0;
			JobLifecycleStatus status = JobLifecycleStatus::Queued;
			std::string errorCode;
		};

		void Initialize(Dependencies deps, Config cfg);
		bool StartWorker();
		void StopWorker();

		blazeclaw::gateway::GatewayHost::ChatRuntimeResult Execute(
			RuntimeExecutionRequest request);
		bool Abort(const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request);
		[[nodiscard]] ChatRuntimeMetricsSnapshot MetricsSnapshot() const;

	private:
		struct ChatRuntimeJob {
			std::uint64_t enqueueSequence = 0;
			std::uint64_t enqueuedAtMs = 0;
			JobLifecycleStatus status = JobLifecycleStatus::Queued;
			blazeclaw::gateway::GatewayHost::ChatRuntimeRequest request;
			std::string sessionId;
			std::string runtimeMessage;
			std::string provider;
			std::string model;
			std::function<blazeclaw::gateway::GatewayHost::ChatRuntimeResult()> execute;
			blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
			std::mutex completionMutex;
			std::condition_variable completionCv;
			bool completed = false;
		};

		void WorkerLoop();
		std::uint64_t CurrentEpochMs() const;
		blazeclaw::gateway::GatewayHost::ChatRuntimeResult BuildErrorResult(
			const std::string& model,
			const std::string& code,
			const std::string& message) const;

		Dependencies m_deps;
		Config m_cfg;

		mutable std::mutex m_queueMutex;
		std::condition_variable m_queueCv;
		std::deque<std::shared_ptr<ChatRuntimeJob>> m_queue;
		std::unordered_map<std::string, std::shared_ptr<ChatRuntimeJob>> m_jobsByRunId;
		std::unordered_map<std::string, ChatRuntimeRunState> m_runsById;
		std::uint64_t m_nextEnqueueSequence = 1;
		std::thread m_workerThread;
		bool m_workerStopRequested = false;
		bool m_workerAvailable = false;
		ChatRuntimeMetrics m_metrics;
	};

} // namespace blazeclaw::core
