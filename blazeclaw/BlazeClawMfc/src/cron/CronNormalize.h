#pragma once

#include "CronModels.h"

namespace blazeclaw::cron {

	class CronNormalize {
	public:
		static std::string ResolveCronId(const CronJson& params);
		static std::string ResolveSessionTarget(const CronJson& params);
		static CronJson NormalizeAddInput(const CronJson& params);
		static void ApplyPatch(CronJson& job, const CronJson& patch);
		static void NormalizeLoadedJob(CronJson& job);
	};

} // namespace blazeclaw::cron
