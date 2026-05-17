#pragma once

#include "CronModels.h"

#include <functional>
#include <optional>

namespace blazeclaw::cron {

	using CronRuntimeExecutionAdapter =
		std::function<std::optional<CronJson>(const CronJson& job, std::int64_t nowMs)>;

	struct CronRuntimeExecutionAdapters {
		CronRuntimeExecutionAdapter mainSession;
		CronRuntimeExecutionAdapter isolatedSession;
	};

	class CronTimerService {
	public:
		void SetRuntimeExecutionAdapters(CronRuntimeExecutionAdapters adapters);

		std::optional<std::int64_t> ComputeNextRunAtMs(
			const CronJson& job,
			std::int64_t nowMs) const;

		bool RecomputeSchedules(
			CronJson& jobs,
			std::int64_t nowMs) const;

		std::int64_t ComputeNextWakeAtMs(const CronJson& jobs) const;

		std::size_t PumpDueRuns(
			CronJson& jobs,
			CronJson& runs,
			std::int64_t nowMs,
			bool forceRunDue = false) const;

	private:
		CronRuntimeExecutionAdapters m_runtimeAdapters;
	};

} // namespace blazeclaw::cron
