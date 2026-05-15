#pragma once

#include "CronModels.h"

#include <filesystem>

namespace blazeclaw::cron {

	class CronStoreService {
	public:
		CronStoreService(
			std::filesystem::path jobsPath,
			std::filesystem::path runsPath);

		void EnsureLoaded();

		CronJson& Jobs();
		CronJson& Runs();
		const std::filesystem::path& JobsPath() const noexcept;

		void SaveJobs() const;
		void SaveRuns() const;

	private:
		std::filesystem::path m_jobsPath;
		std::filesystem::path m_runsPath;
		bool m_loaded = false;
		CronJson m_jobs = CronJson::array();
		CronJson m_runs = CronJson::array();

		static CronJson LoadArrayFile(const std::filesystem::path& path);
		static void SaveArrayFile(
			const std::filesystem::path& path,
			const CronJson& values);
	};

} // namespace blazeclaw::cron
