#pragma once

#include "CronModels.h"

#include <optional>

namespace blazeclaw::cron {

	class CronTimerService {
	public:
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
	};

} // namespace blazeclaw::cron
