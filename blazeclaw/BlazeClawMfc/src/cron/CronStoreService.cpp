#include "pch.h"

#include "CronStoreService.h"

#include "CronNormalize.h"

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
	}

	CronStoreService::CronStoreService(
		std::filesystem::path jobsPath,
		std::filesystem::path runsPath)
		: m_jobsPath(std::move(jobsPath)),
		m_runsPath(std::move(runsPath)) {
	}

	void CronStoreService::EnsureLoaded() {
		if (m_loaded) {
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

	void CronStoreService::SaveJobs() const {
		SaveArrayFile(m_jobsPath, m_jobs);
	}

	void CronStoreService::SaveRuns() const {
		SaveArrayFile(m_runsPath, m_runs);
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
			}
		}
		catch (...) {
		}

		return CronJson::array();
	}

	void CronStoreService::SaveArrayFile(
		const std::filesystem::path& path,
		const CronJson& values) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);

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
