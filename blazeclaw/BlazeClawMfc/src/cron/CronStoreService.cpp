#include "pch.h"

#include "CronStoreService.h"

#include "CronNormalize.h"

#include <chrono>
#include <fstream>

namespace blazeclaw::cron {

	namespace {
		inline constexpr std::int64_t kCronStoreVersion = 1;

		CronJson BuildEnvelope(
			const char* kind,
			const CronJson& values) {
			return {
				{ "version", kCronStoreVersion },
				{ "kind", kind },
				{ "values", values }
			};
		}

		CronJson ParseArrayPayload(std::istream& stream) {
			CronJson parsed;
			stream >> parsed;
			if (parsed.is_array()) {
				return parsed;
			}
			if (parsed.is_object()) {
				const auto valuesIt = parsed.find("values");
				if (valuesIt != parsed.end() && valuesIt->is_array()) {
					return *valuesIt;
				}

				const auto itemsIt = parsed.find("items");
				if (itemsIt != parsed.end() && itemsIt->is_array()) {
					return *itemsIt;
				}

				const auto dataIt = parsed.find("data");
				if (dataIt != parsed.end() && dataIt->is_array()) {
					return *dataIt;
				}
			}

			return CronJson::array();
		}

		std::filesystem::file_time_type ReadLastWriteTime(
			const std::filesystem::path& path) {
			std::error_code ec;
			const std::filesystem::file_time_type writeTime =
				std::filesystem::last_write_time(path, ec);
			if (ec) {
				return std::filesystem::file_time_type{};
			}
			return writeTime;
		}
	}

	CronStoreService::CronStoreService(
		std::filesystem::path jobsPath,
		std::filesystem::path runsPath)
		: m_jobsPath(std::move(jobsPath)),
		m_runsPath(std::move(runsPath)) {
	}

	void CronStoreService::EnsureLoaded(bool forceReload) {
		const std::filesystem::file_time_type currentJobsWriteTime =
			ReadLastWriteTime(m_jobsPath);
		const std::filesystem::file_time_type currentRunsWriteTime =
			ReadLastWriteTime(m_runsPath);

		if (m_loaded && !forceReload &&
			currentJobsWriteTime == m_jobsLastWriteTime &&
			currentRunsWriteTime == m_runsLastWriteTime) {
			return;
		}

		std::error_code ec;
		std::filesystem::create_directories(m_jobsPath.parent_path(), ec);

		m_jobs = LoadArrayFile(m_jobsPath);
		m_runs = LoadArrayFile(m_runsPath);
		for (auto& job : m_jobs) {
			CronNormalize::NormalizeLoadedJob(job);
		}

		m_loaded = true;
		m_jobsLastWriteTime = currentJobsWriteTime;
		m_runsLastWriteTime = currentRunsWriteTime;
	}

	CronJson& CronStoreService::Jobs() {
		return m_jobs;
	}

	CronJson& CronStoreService::Runs() {
		return m_runs;
	}

	const std::filesystem::path& CronStoreService::JobsPath() const noexcept {
		return m_jobsPath;
	}

	void CronStoreService::SaveJobs() {
		SaveArrayFile(m_jobsPath, m_jobs);
		m_jobsLastWriteTime = ReadLastWriteTime(m_jobsPath);
	}

	void CronStoreService::SaveRuns() {
		SaveArrayFile(m_runsPath, m_runs);
		m_runsLastWriteTime = ReadLastWriteTime(m_runsPath);
	}

	CronJson CronStoreService::LoadArrayFile(const std::filesystem::path& path) {
		if (!std::filesystem::exists(path)) {
			return CronJson::array();
		}

		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			return CronJson::array();
		}

		try {
			return ParseArrayPayload(stream);
		}
		catch (...) {
			const std::filesystem::path backupPath = path.string() + ".bak";
			std::ifstream backup(backupPath, std::ios::binary);
			if (!backup.is_open()) {
				return CronJson::array();
			}

			try {
				return ParseArrayPayload(backup);
			}
			catch (...) {
			}
		}

		return CronJson::array();
	}

	void CronStoreService::SaveArrayFile(
		const std::filesystem::path& path,
		const CronJson& values) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (std::filesystem::exists(path)) {
			const std::filesystem::path backupPath = path.string() + ".bak";
			std::filesystem::copy_file(
				path,
				backupPath,
				std::filesystem::copy_options::overwrite_existing,
				ec);
			ec.clear();
		}

		const std::filesystem::path tempPath = path.string() + ".tmp";
		std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return;
		}

		const std::string kind =
			path.filename().string().find("runs") != std::string::npos
			? "runs"
			: "jobs";
		const CronJson envelope = BuildEnvelope(kind.c_str(), values);
		stream << envelope.dump(2);
		stream.flush();
		stream.close();

		std::filesystem::rename(tempPath, path, ec);
		if (ec) {
			std::filesystem::remove(path, ec);
			ec.clear();
			std::filesystem::rename(tempPath, path, ec);
			if (ec) {
				std::filesystem::remove(tempPath, ec);
			}
		}
	}

} // namespace blazeclaw::cron
