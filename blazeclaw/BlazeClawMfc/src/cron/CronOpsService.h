#pragma once

#include "CronNormalize.h"
#include "CronStoreService.h"
#include "CronTimerService.h"

#include <mutex>

namespace blazeclaw::cron {

	class CronOpsService {
	public:
		CronOpsService();

		CronJson Status(const CronJson& params);
		CronJson List(const CronJson& params);
		CronJson Add(const CronJson& params);
		CronJson Update(const CronJson& params);
		CronJson Remove(const CronJson& params);
		CronJson Run(const CronJson& params);
		CronJson Runs(const CronJson& params);
		CronJson Wake(const CronJson& params);

	private:
		std::mutex m_mutex;
		CronStoreService m_store;
		CronTimerService m_timer;
		std::uint64_t m_idCounter = 0;
		bool m_startupCatchupDone = false;
		std::int64_t m_lastSyncAtMs = 0;
		std::size_t m_maxCatchupRunsPerSync = 64;

		static std::size_t ClampLimit(
			const CronJson& value,
			std::size_t min,
			std::size_t max,
			std::size_t fallback);

		CronJson* FindJobByIdLocked(const std::string& id);
		void EnsureLoadedLocked();
		void RunStartupCatchupLocked();
		void RefreshSchedulesOnlyLocked(std::int64_t nowMs);
		void SyncDueRunsLocked(std::int64_t nowMs, bool forceRunDue = false);
	};

	CronOpsService& GetCronOpsService();

} // namespace blazeclaw::cron
