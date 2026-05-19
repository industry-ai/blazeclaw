#pragma once

#include "CronNormalize.h"
#include "CronStoreService.h"
#include "CronTimerService.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace blazeclaw::cron {

	class CronOpsService {
	public:
		using TaskLedgerHook = std::function<void(const CronJson& payload)>;

		struct TaskLedgerHooks {
			TaskLedgerHook createRunningTaskRun;
			TaskLedgerHook completeTaskRunByRunId;
			TaskLedgerHook failTaskRunByRunId;
		};

		using ScheduleNotificationHook =
			std::function<void(const CronScheduleNotificationEvent& event)>;

		struct ScheduleNotificationHooks {
			ScheduleNotificationHook enqueueSystemEvent;
			ScheduleNotificationHook requestHeartbeatNow;
		};

		CronOpsService();
		~CronOpsService();

		CronJson Status(const CronJson& params);
		CronJson List(const CronJson& params);
		CronJson Add(const CronJson& params);
		CronJson Update(const CronJson& params);
		CronJson Remove(const CronJson& params);
		CronJson Run(const CronJson& params);
		CronJson Runs(const CronJson& params);
		CronJson Wake(const CronJson& params);

		void SetRuntimeExecutionAdapters(CronRuntimeExecutionAdapters adapters);
		void SetTaskLedgerHooks(TaskLedgerHooks hooks);
		void SetScheduleNotificationHooks(ScheduleNotificationHooks hooks);
		void EnqueueDeferredWakeRequest(const CronJson& wakeParams);

		void StartBackgroundScheduler();
		void StopBackgroundScheduler();

	private:
		std::mutex m_mutex;
		std::mutex m_backgroundMutex;
		std::condition_variable m_backgroundCv;
		std::thread m_backgroundThread;
		bool m_backgroundStopRequested = false;
		bool m_backgroundStarted = false;
		CronStoreService m_store;
		CronTimerService m_timer;
		std::uint64_t m_idCounter = 0;
		bool m_startupCatchupDone = false;
		std::int64_t m_lastSyncAtMs = 0;
		std::size_t m_maxCatchupRunsPerSync = 64;

		struct ManualRunRequest {
			std::string jobId;
			std::string mode;
			std::string runId;
			std::int64_t queuedAtMs = 0;
		};

		std::deque<ManualRunRequest> m_manualRunQueue;
		std::uint64_t m_manualRunCounter = 0;
		TaskLedgerHooks m_taskLedgerHooks;
		ScheduleNotificationHooks m_scheduleNotificationHooks;
		std::mutex m_deferredWakeMutex;
		std::deque<CronJson> m_deferredWakeRequests;

		class ScheduleNotificationFlushScope {
		public:
			explicit ScheduleNotificationFlushScope(CronOpsService& ops);

			ScheduleNotificationFlushScope(const ScheduleNotificationFlushScope&) = delete;
			ScheduleNotificationFlushScope& operator=(
				const ScheduleNotificationFlushScope&) = delete;

			std::vector<CronScheduleNotificationEvent>& notifications();

			~ScheduleNotificationFlushScope();

		private:
			CronOpsService& m_ops;
			std::vector<CronScheduleNotificationEvent> m_notifications;
		};

		static std::size_t ClampLimit(
			const CronJson& value,
			std::size_t min,
			std::size_t max,
			std::size_t fallback);

		CronJson* FindJobByIdLocked(const std::string& id);
		void EnsureLoadedLocked();
		void RunStartupCatchupLocked(
			std::vector<CronScheduleNotificationEvent>* notifications = nullptr);
		void BackgroundSchedulerLoop();
		void ProcessManualRunQueueLocked(
			std::int64_t nowMs,
			std::vector<CronScheduleNotificationEvent>* notifications = nullptr);
		void RefreshSchedulesOnlyLocked(
			std::int64_t nowMs,
			std::vector<CronScheduleNotificationEvent>* notifications = nullptr);
		void SyncDueRunsLocked(
			std::int64_t nowMs,
			bool forceRunDue = false,
			std::vector<CronScheduleNotificationEvent>* notifications = nullptr);
		void FlushScheduleNotifications(
			std::vector<CronScheduleNotificationEvent>& notifications);
		void ProcessDeferredWakeRequests();
		void EmitTaskLedgerCreateRunningHook(const CronJson& runEntry);
		void EmitTaskLedgerTerminalHook(const CronJson& runEntry);
	};

	CronOpsService& GetCronOpsService();

} // namespace blazeclaw::cron
