#pragma once

#include "CronModels.h"

#include <filesystem>

namespace blazeclaw::cron {

	class CronStoreService {
	public:
		CronStoreService(
			std::filesystem::path jobsPath,
			std::filesystem::path runsPath);

		void EnsureLoaded(bool forceReload = false);

		CronJson& Jobs();
		CronJson& Runs();
		const std::filesystem::path& JobsPath() const noexcept;

		void SaveJobs();
		void SaveRuns();

	private:
		std::filesystem::path m_jobsPath;
		std::filesystem::path m_runsPath;
		bool m_loaded = false;
		std::filesystem::file_time_type m_jobsLastWriteTime{};
		std::filesystem::file_time_type m_runsLastWriteTime{};
		CronJson m_jobs = CronJson::array();
		CronJson m_runs = CronJson::array();

		static CronJson LoadArrayFile(const std::filesystem::path& path);
		static void SaveArrayFile(
			const std::filesystem::path& path,
			const CronJson& values);
	};

} // namespace blazeclaw::cron
