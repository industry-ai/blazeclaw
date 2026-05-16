#pragma once

#include "CronNormalize.h"
#include "CronStoreService.h"
#include "CronTimerService.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace blazeclaw::cron {

	class CronOpsService {
	public:
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

		static std::size_t ClampLimit(
			const CronJson& value,
			std::size_t min,
			std::size_t max,
			std::size_t fallback);

		CronJson* FindJobByIdLocked(const std::string& id);
		void EnsureLoadedLocked();
		void RunStartupCatchupLocked();
		void BackgroundSchedulerLoop();
		void ProcessManualRunQueueLocked(std::int64_t nowMs);
		void RefreshSchedulesOnlyLocked(std::int64_t nowMs);
		void SyncDueRunsLocked(std::int64_t nowMs, bool forceRunDue = false);
	};

	CronOpsService& GetCronOpsService();

} // namespace blazeclaw::cron
