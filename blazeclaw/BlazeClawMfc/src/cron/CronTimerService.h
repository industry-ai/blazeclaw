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
		// When true (production wiring), registered adapters are authoritative and
		// simulation fallback runs only for explicit handled=false or unregistered lanes.
		bool preferRuntimeExecution = false;
	};

	struct CronRecomputeOptions {
		bool preserveDueSlots = false;
	};

	class CronTimerService {
	public:
		void SetRuntimeExecutionAdapters(CronRuntimeExecutionAdapters adapters);

		std::optional<std::int64_t> ComputeNextRunAtMs(
			const CronJson& job,
			std::int64_t nowMs) const;

		bool RecomputeSchedules(
			CronJson& jobs,
			std::int64_t nowMs,
			const CronRecomputeOptions& opts = {}) const;

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
