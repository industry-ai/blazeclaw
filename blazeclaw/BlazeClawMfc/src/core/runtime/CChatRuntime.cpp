#include "pch.h"
#include "CChatRuntime.h"

#include <algorithm>
#include <chrono>

namespace blazeclaw::core {

	void CChatRuntime::Initialize(Dependencies deps, Config cfg)
	{
		m_deps = std::move(deps);
		m_cfg = std::move(cfg);
	}

	bool CChatRuntime::StartWorker()
	{
		std::lock_guard<std::mutex> lock(m_queueMutex);
		if (m_workerThread.joinable())
		{
			m_workerAvailable = true;
			return true;
		}

		m_workerStopRequested = false;
		try
		{
			m_workerThread = std::thread([this]()
				{
					WorkerLoop();
				});
			m_workerAvailable = true;
			return true;
		}
		catch (...)
		{
			m_workerAvailable = false;
			return false;
		}
	}

	void CChatRuntime::StopWorker()
	{
		std::vector<std::shared_ptr<ChatRuntimeJob>> abandonedJobs;
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			m_workerStopRequested = true;
			m_workerAvailable = false;
			abandonedJobs.assign(m_queue.begin(), m_queue.end());
			m_queue.clear();
			m_jobsByRunId.clear();
			for (const auto& job : abandonedJobs)
			{
				auto stateIt = m_runsById.find(job->request.runId);
				if (stateIt != m_runsById.end())
				{
					stateIt->second.status = JobLifecycleStatus::Failed;
					stateIt->second.completedAtMs = CurrentEpochMs();
					stateIt->second.errorCode = m_cfg.errorWorkerUnavailable;
				}
			}
		}

		m_queueCv.notify_all();

		for (const auto& job : abandonedJobs)
		{
			std::lock_guard<std::mutex> completionLock(job->completionMutex);
			job->result = BuildErrorResult(
				job->model,
				m_cfg.errorWorkerUnavailable,
				"chat runtime worker unavailable");
			job->completed = true;
			job->completionCv.notify_all();
		}

		if (m_workerThread.joinable())
		{
			m_workerThread.join();
		}

		std::lock_guard<std::mutex> lock(m_queueMutex);
		m_queue.clear();
		m_jobsByRunId.clear();
		m_runsById.clear();
		m_nextEnqueueSequence = 1;
		m_workerStopRequested = false;
	}

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult CChatRuntime::Execute(
		RuntimeExecutionRequest request)
	{
		if (!m_cfg.asyncQueueEnabled)
		{
			return request.execute();
		}

		const std::uint64_t enqueuedAtMs = CurrentEpochMs();
		auto job = std::make_shared<ChatRuntimeJob>();
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			if (!m_workerAvailable)
			{
				return BuildErrorResult(
					request.model,
					m_cfg.errorWorkerUnavailable,
					"chat runtime worker unavailable");
			}

			if (m_queue.size() >= m_cfg.queueCapacity)
			{
				return BuildErrorResult(
					request.model,
					m_cfg.errorQueueFull,
					"chat runtime queue capacity reached");
			}

			job->enqueueSequence = m_nextEnqueueSequence++;
			job->enqueuedAtMs = enqueuedAtMs;
			job->status = JobLifecycleStatus::Queued;
			job->request = request.request;
			job->sessionId = request.sessionId;
			job->runtimeMessage = request.runtimeMessage;
			job->provider = request.provider;
			job->model = request.model;
			job->execute = std::move(request.execute);

			m_queue.push_back(job);
			m_jobsByRunId.insert_or_assign(request.request.runId, job);
			m_runsById[request.request.runId] = ChatRuntimeRunState{
				.runId = request.request.runId,
				.sessionId = request.sessionId,
				.provider = request.provider,
				.model = request.model,
				.enqueuedAtMs = enqueuedAtMs,
				.startedAtMs = 0,
				.completedAtMs = 0,
				.status = JobLifecycleStatus::Queued,
				.errorCode = {},
			};
		}

		m_queueCv.notify_one();

		std::unique_lock<std::mutex> completionLock(job->completionMutex);
		const auto waitBudget = std::chrono::milliseconds(
			m_cfg.queueWaitTimeoutMs +
			m_cfg.executionTimeoutMs +
			1000);
		if (!job->completionCv.wait_for(completionLock, waitBudget, [job]()
			{
				return job->completed;
			}))
		{
			if (m_deps.onQueueTimeout)
			{
				m_deps.onQueueTimeout(job->request.runId, job->provider);
			}

			if (m_deps.cancelActiveRuntime)
			{
				const bool cancelled = m_deps.cancelActiveRuntime(job->request.runId);
				(void)cancelled;
			}

			{
				std::lock_guard<std::mutex> lock(m_queueMutex);
				std::erase_if(
					m_queue,
					[&](const std::shared_ptr<ChatRuntimeJob>& queuedJob)
					{
						return queuedJob->request.runId == job->request.runId;
					});
				m_jobsByRunId.erase(job->request.runId);
				m_runsById.erase(job->request.runId);
			}

			if (m_deps.onQueueTimeoutCleanup)
			{
				m_deps.onQueueTimeoutCleanup(job->request.runId, job->provider);
			}

			return BuildErrorResult(
				job->model,
				m_cfg.errorTimedOut,
				"chat runtime timed out");
		}

		return job->result;
	}

	bool CChatRuntime::Abort(
		const blazeclaw::gateway::GatewayHost::ChatAbortRequest& request)
	{
		std::shared_ptr<ChatRuntimeJob> queuedJob;
		std::string runProvider;
		bool removedQueuedJob = false;
		{
			std::lock_guard<std::mutex> lock(m_queueMutex);
			runProvider.clear();

			auto stateIt = m_runsById.find(request.runId);
			if (stateIt != m_runsById.end())
			{
				runProvider = stateIt->second.provider;
				stateIt->second.status = JobLifecycleStatus::Cancelled;
				stateIt->second.completedAtMs = CurrentEpochMs();
				stateIt->second.errorCode = m_cfg.errorCancelled;
			}

			auto jobIt = m_jobsByRunId.find(request.runId);
			if (jobIt != m_jobsByRunId.end())
			{
				queuedJob = jobIt->second;
				auto queueIt = std::find(m_queue.begin(), m_queue.end(), queuedJob);
				if (queueIt != m_queue.end())
				{
					m_queue.erase(queueIt);
					removedQueuedJob = true;
					m_jobsByRunId.erase(jobIt);
					m_runsById.erase(request.runId);
				}
			}
		}

		if (removedQueuedJob && queuedJob)
		{
			std::lock_guard<std::mutex> completionLock(queuedJob->completionMutex);
			queuedJob->result = BuildErrorResult(
				queuedJob->model,
				m_cfg.errorCancelled,
				"chat runtime cancelled");
			queuedJob->completed = true;
			queuedJob->completionCv.notify_all();
		}

		if (m_deps.onAbort)
		{
			m_deps.onAbort(request.runId, runProvider, removedQueuedJob);
		}

		bool cancelled = removedQueuedJob;
		if (m_deps.cancelActiveRuntime)
		{
			cancelled = m_deps.cancelActiveRuntime(request.runId) || cancelled;
		}

		return cancelled;
	}

	void CChatRuntime::WorkerLoop()
	{
		for (;;)
		{
			std::shared_ptr<ChatRuntimeJob> job;
			bool cancelledBeforeExecution = false;
			bool timedOutBeforeExecution = false;
			{
				std::unique_lock<std::mutex> lock(m_queueMutex);
				m_queueCv.wait(lock, [this]()
					{
						return m_workerStopRequested || !m_queue.empty();
					});

				if (m_workerStopRequested && m_queue.empty())
				{
					break;
				}

				job = m_queue.front();
				m_queue.pop_front();
				auto stateIt = m_runsById.find(job->request.runId);
				if (stateIt != m_runsById.end())
				{
					const std::uint64_t nowMs = CurrentEpochMs();
					cancelledBeforeExecution =
						stateIt->second.status == JobLifecycleStatus::Cancelled;
					timedOutBeforeExecution =
						nowMs > stateIt->second.enqueuedAtMs &&
						(nowMs - stateIt->second.enqueuedAtMs) >
						m_cfg.queueWaitTimeoutMs;
					stateIt->second.status = JobLifecycleStatus::Started;
					stateIt->second.startedAtMs = nowMs;
				}
			}

			blazeclaw::gateway::GatewayHost::ChatRuntimeResult result;
			if (cancelledBeforeExecution)
			{
				result = BuildErrorResult(
					job->model,
					m_cfg.errorCancelled,
					"chat runtime cancelled");
			}
			else if (timedOutBeforeExecution)
			{
				result = BuildErrorResult(
					job->model,
					m_cfg.errorTimedOut,
					"chat runtime timed out before execution");
			}
			else if (job->execute)
			{
				const std::uint64_t executeStartedAtMs = CurrentEpochMs();
				result = job->execute();
				const std::uint64_t executeCompletedAtMs = CurrentEpochMs();
				if (executeCompletedAtMs > executeStartedAtMs &&
					(executeCompletedAtMs - executeStartedAtMs) >
					m_cfg.executionTimeoutMs)
				{
					result = BuildErrorResult(
						job->model,
						m_cfg.errorTimedOut,
						"chat runtime execution timed out");
				}

				if (m_deps.isRunCancelled &&
					m_deps.isRunCancelled(job->request.runId, job->provider))
				{
					result = BuildErrorResult(
						job->model,
						m_cfg.errorCancelled,
						"chat runtime cancelled");
				}
			}
			else
			{
				result = BuildErrorResult(
					job->model,
					m_cfg.errorWorkerUnavailable,
					"chat runtime worker unavailable");
			}

			{
				std::lock_guard<std::mutex> lock(m_queueMutex);
				auto stateIt = m_runsById.find(job->request.runId);
				if (stateIt != m_runsById.end())
				{
					stateIt->second.completedAtMs = CurrentEpochMs();
					stateIt->second.errorCode = result.errorCode;
					if (result.ok)
					{
						stateIt->second.status = JobLifecycleStatus::Completed;
					}
					else if (result.errorCode == m_cfg.errorCancelled)
					{
						stateIt->second.status = JobLifecycleStatus::Cancelled;
					}
					else if (result.errorCode == m_cfg.errorTimedOut)
					{
						stateIt->second.status = JobLifecycleStatus::TimedOut;
					}
					else
					{
						stateIt->second.status = JobLifecycleStatus::Failed;
					}

					m_runsById.erase(stateIt);
				}

				m_jobsByRunId.erase(job->request.runId);
			}

			if (m_deps.onWorkerCompleted)
			{
				m_deps.onWorkerCompleted(job->request.runId, job->provider);
			}

			std::lock_guard<std::mutex> completionLock(job->completionMutex);
			job->result = std::move(result);
			job->completed = true;
			job->completionCv.notify_all();
		}
	}

	std::uint64_t CChatRuntime::CurrentEpochMs() const
	{
		const auto now = std::chrono::system_clock::now();
		return static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				now.time_since_epoch())
			.count());
	}

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult CChatRuntime::BuildErrorResult(
		const std::string& model,
		const std::string& code,
		const std::string& message) const
	{
		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = false,
			.assistantText = {},
			.modelId = model,
			.errorCode = code,
			.errorMessage = message,
		};
	}

} // namespace blazeclaw::core
