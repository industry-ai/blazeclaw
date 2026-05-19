#pragma once

#include "CronModels.h"

#include <cstddef>
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
		const std::filesystem::path& RunsPath() const noexcept;
		const std::filesystem::path& RunsDir() const noexcept;

		void SaveJobs();
		void SaveRuns();

		static std::filesystem::path ResolveRunsDir(
			const std::filesystem::path& jobsPath);
		static std::filesystem::path ResolveRunLogPath(
			const std::filesystem::path& jobsPath,
			const std::string& jobId);
		static bool IsSafeRunLogJobId(const std::string& jobId);

	private:
		std::filesystem::path m_jobsPath;
		std::filesystem::path m_runsPath;
		std::filesystem::path m_runsDir;
		bool m_loaded = false;
		std::filesystem::file_time_type m_jobsLastWriteTime{};
		std::filesystem::file_time_type m_runsLastWriteTime{};
		std::filesystem::file_time_type m_runsDirLastWriteTime{};
		CronJson m_jobs = CronJson::array();
		CronJson m_runs = CronJson::array();
		std::size_t m_runsPersistedCount = 0;

		void MigrateLegacyRunsToJsonlIfNeeded();
		void AppendRunEntryToJobLog(const CronJson& runEntry);
		static std::string BuildRunDedupeKey(const CronJson& runEntry);
		static CronJson LoadArrayFile(
			const std::filesystem::path& path,
			const char* expectedKind);
		static CronJson LoadRunsFromJsonlDir(const std::filesystem::path& runsDir);
		static CronJson MergeRunEntries(
			const CronJson& aggregateRuns,
			const CronJson& jsonlRuns);
		static void SaveArrayFile(
			const std::filesystem::path& path,
			const CronJson& values);
		static std::filesystem::file_time_type ReadRunsDirWriteTime(
			const std::filesystem::path& runsDir);
	};

} // namespace blazeclaw::cron
