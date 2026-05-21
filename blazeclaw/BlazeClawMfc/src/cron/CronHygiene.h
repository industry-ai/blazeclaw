#pragma once

#include "CronModels.h"

#include <cstddef>
#include <string>

namespace blazeclaw::cron {

	struct CronScheduleValidation {
		bool ok = true;
		std::string message;
	};

	CronScheduleValidation ValidateScheduleTimestamp(
		const CronJson& schedule,
		std::int64_t nowMs = 0);

	void MarkCronJobActive(const std::string& jobId);
	void ClearCronJobActive(const std::string& jobId);
	bool IsCronJobActive(const std::string& jobId);
	void ResetCronActiveJobsForTests();

	void RecordCronRunSession(
		CronJson& job,
		const std::string& sessionKey,
		std::int64_t nowMs);
	std::size_t SweepCronRunSessions(CronJson& jobs, std::int64_t nowMs);

} // namespace blazeclaw::cron
