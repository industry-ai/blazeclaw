#include "pch.h"

#include "CronStoreService.h"

#include "CronJsonCompat.h"
#include "CronNormalize.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace blazeclaw::cron {

	namespace {
		inline constexpr std::int64_t kCronStoreVersion = 1;
		inline constexpr std::size_t kDefaultMaxRunLogLinesPerJob = 2000;

		std::size_t ResolveMaxRunLogLinesPerJob() {
			char* value = nullptr;
			size_t valueLength = 0;
			if (_dupenv_s(
				&value,
				&valueLength,
				"BLAZECLAW_CRON_RUN_LOG_MAX_LINES") != 0 || value == nullptr) {
				return kDefaultMaxRunLogLinesPerJob;
			}

			const std::string trimmed = TrimCopy(value);
			free(value);
			if (trimmed.empty()) {
				return kDefaultMaxRunLogLinesPerJob;
			}

			try {
				const long long parsed = std::stoll(trimmed);
				if (parsed <= 0) {
					return 0;
				}
				return static_cast<std::size_t>(parsed);
			}
			catch (...) {
				return kDefaultMaxRunLogLinesPerJob;
			}
		}

		void PruneRunLogFileToMaxLines(
			const std::filesystem::path& logPath,
			const std::size_t maxLines) {
			if (maxLines == 0 || !std::filesystem::exists(logPath)) {
				return;
			}

			std::ifstream readStream(logPath, std::ios::binary);
			if (!readStream.is_open()) {
				return;
			}

			std::deque<std::string> tail;
			std::size_t lineCount = 0;
			std::string line;
			while (std::getline(readStream, line)) {
				++lineCount;
				tail.push_back(line);
				if (tail.size() > maxLines) {
					tail.pop_front();
				}
			}

			if (lineCount <= maxLines) {
				return;
			}

			std::ofstream writeStream(logPath, std::ios::binary | std::ios::trunc);
			if (!writeStream.is_open()) {
				return;
			}

			for (const std::string& keptLine : tail) {
				writeStream << keptLine << '\n';
			}
		}

		CronJson BuildEnvelope(
			const char* kind,
			const CronJson& values) {
			return {
				{ "version", kCronStoreVersion },
				{ "kind", kind },
				{ "values", values }
			};
		}

		CronJson ParseArrayPayload(
			const CronJson& parsed,
			const char* expectedKind) {
			if (parsed.is_array()) {
				return parsed;
			}
			if (!parsed.is_object()) {
				return CronJson::array();
			}

			const std::string kind =
				parsed.contains("kind") && parsed["kind"].is_string()
				? parsed["kind"].get<std::string>()
				: std::string();
			const std::string normalizedKind = ToLowerCopy(TrimCopy(kind));
			const std::string normalizedExpected =
				ToLowerCopy(TrimCopy(expectedKind ? expectedKind : ""));

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

			if (!normalizedExpected.empty()) {
				const auto expectedIt = parsed.find(normalizedExpected);
				if (expectedIt != parsed.end() && expectedIt->is_array()) {
					return *expectedIt;
				}
			}

			if (normalizedKind == "jobs" || normalizedKind == "runs") {
				const auto kindArrayIt = parsed.find(normalizedKind);
				if (kindArrayIt != parsed.end() && kindArrayIt->is_array()) {
					return *kindArrayIt;
				}
			}

			// OpenClaw jobs.json shape: { "version": 1, "jobs": [...] } (no kind/values).
			const auto jobsIt = parsed.find("jobs");
			if (jobsIt != parsed.end() && jobsIt->is_array() &&
				(normalizedExpected == "jobs" || normalizedExpected.empty())) {
				return *jobsIt;
			}

			const auto runsIt = parsed.find("runs");
			if (runsIt != parsed.end() && runsIt->is_array() &&
				normalizedExpected == "runs") {
				return *runsIt;
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

		bool IsFinishedRunLogLine(const CronJson& entry) {
			if (!entry.is_object()) {
				return false;
			}
			const std::string action = ToLowerCopy(
				TrimCopy(entry.value("action", std::string())));
			if (action == "finished") {
				return true;
			}
			return entry.contains("jobId") &&
				(entry.contains("status") || entry.contains("runId") || entry.contains("ts"));
		}

	} // namespace

	std::filesystem::path CronStoreService::ResolveRunsDir(
		const std::filesystem::path& jobsPath) {
		return jobsPath.parent_path() / "runs";
	}

	bool CronStoreService::IsSafeRunLogJobId(const std::string& jobId) {
		const std::string trimmed = TrimCopy(jobId);
		if (trimmed.empty()) {
			return false;
		}
		return trimmed.find('/') == std::string::npos &&
			trimmed.find('\\') == std::string::npos &&
			trimmed.find('\0') == std::string::npos;
	}

	std::filesystem::path CronStoreService::ResolveRunLogPath(
		const std::filesystem::path& jobsPath,
		const std::string& jobId) {
		const std::filesystem::path runsDir = ResolveRunsDir(jobsPath);
		if (!IsSafeRunLogJobId(jobId)) {
			throw std::invalid_argument("invalid cron run log job id");
		}
		const std::filesystem::path resolved = runsDir / (TrimCopy(jobId) + ".jsonl");
		if (resolved.parent_path() != runsDir) {
			throw std::invalid_argument("invalid cron run log job id");
		}
		return resolved;
	}

	CronStoreService::CronStoreService(
		std::filesystem::path jobsPath,
		std::filesystem::path runsPath)
		: m_jobsPath(std::move(jobsPath)),
		m_runsPath(std::move(runsPath)),
		m_runsDir(ResolveRunsDir(m_jobsPath)) {
	}

	void CronStoreService::EnsureLoaded(bool forceReload) {
		const std::filesystem::file_time_type currentJobsWriteTime =
			ReadLastWriteTime(m_jobsPath);
		const std::filesystem::file_time_type currentRunsWriteTime =
			ReadLastWriteTime(m_runsPath);
		const std::filesystem::file_time_type currentRunsDirWriteTime =
			ReadRunsDirWriteTime(m_runsDir);

		if (m_loaded && !forceReload &&
			currentJobsWriteTime == m_jobsLastWriteTime &&
			currentRunsWriteTime == m_runsLastWriteTime &&
			currentRunsDirWriteTime == m_runsDirLastWriteTime) {
			return;
		}

		std::error_code ec;
		std::filesystem::create_directories(m_jobsPath.parent_path(), ec);
		std::filesystem::create_directories(m_runsDir, ec);

		m_jobs = LoadArrayFile(m_jobsPath, "jobs");
		const CronJson aggregateRuns = LoadArrayFile(m_runsPath, "runs");
		const CronJson jsonlRuns = LoadRunsFromJsonlDir(m_runsDir);
		m_runs = MergeRunEntries(aggregateRuns, jsonlRuns);
		for (auto& job : m_jobs) {
			CronNormalize::NormalizeLoadedJob(job);
		}

		m_loaded = true;
		m_jobsLastWriteTime = currentJobsWriteTime;
		m_runsLastWriteTime = currentRunsWriteTime;
		m_runsDirLastWriteTime = currentRunsDirWriteTime;
		m_runsPersistedCount = m_runs.size();
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

	const std::filesystem::path& CronStoreService::RunsPath() const noexcept {
		return m_runsPath;
	}

	const std::filesystem::path& CronStoreService::RunsDir() const noexcept {
		return m_runsDir;
	}

	void CronStoreService::SaveJobs() {
		SaveArrayFile(m_jobsPath, m_jobs);
		m_jobsLastWriteTime = ReadLastWriteTime(m_jobsPath);
	}

	void CronStoreService::SaveRuns() {
		MigrateLegacyRunsToJsonlIfNeeded();

		for (std::size_t index = m_runsPersistedCount; index < m_runs.size(); ++index) {
			AppendRunEntryToJobLog(m_runs[index]);
		}
		m_runsPersistedCount = m_runs.size();

		SaveArrayFile(m_runsPath, m_runs);
		m_runsLastWriteTime = ReadLastWriteTime(m_runsPath);
		m_runsDirLastWriteTime = ReadRunsDirWriteTime(m_runsDir);
	}

	std::string CronStoreService::BuildRunDedupeKey(const CronJson& runEntry) {
		if (!runEntry.is_object()) {
			return {};
		}
		const std::string runId = TrimCopy(runEntry.value("runId", std::string()));
		if (!runId.empty()) {
			return "runId:" + runId;
		}
		const std::string jobId = TrimCopy(runEntry.value("jobId", std::string()));
		const std::int64_t ts = runEntry.value("ts", static_cast<std::int64_t>(0));
		const std::string status = TrimCopy(runEntry.value("status", std::string()));
		return "ts:" + jobId + ":" + std::to_string(ts) + ":" + status;
	}

	CronJson CronStoreService::MergeRunEntries(
		const CronJson& aggregateRuns,
		const CronJson& jsonlRuns) {
		std::unordered_map<std::string, CronJson> merged;
		auto ingest = [&merged](const CronJson& runs) {
			if (!runs.is_array()) {
				return;
			}
			for (const auto& entry : runs) {
				if (!entry.is_object()) {
					continue;
				}
				const std::string key = BuildRunDedupeKey(entry);
				if (key.empty()) {
					continue;
				}
				merged[key] = entry;
			}
		};

		ingest(aggregateRuns);
		ingest(jsonlRuns);

		CronJson combined = CronJson::array();
		combined.get_ref<CronJson::array_t&>().reserve(merged.size());
		for (const auto& [key, entry] : merged) {
			(void)key;
			combined.push_back(entry);
		}

		std::sort(
			combined.begin(),
			combined.end(),
			[](const CronJson& left, const CronJson& right) {
				const std::int64_t leftTs = left.value("ts", static_cast<std::int64_t>(0));
				const std::int64_t rightTs = right.value("ts", static_cast<std::int64_t>(0));
				return leftTs > rightTs;
			});
		return combined;
	}

	CronJson CronStoreService::LoadRunsFromJsonlDir(
		const std::filesystem::path& runsDir) {
		CronJson runs = CronJson::array();
		if (!std::filesystem::exists(runsDir)) {
			return runs;
		}

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(runsDir, ec)) {
			if (ec || !entry.is_regular_file()) {
				continue;
			}
			const std::string filename = entry.path().filename().string();
			if (filename.size() < 6 ||
				filename.compare(filename.size() - 6, 6, ".jsonl") != 0) {
				continue;
			}

			std::ifstream stream(entry.path(), std::ios::binary);
			if (!stream.is_open()) {
				continue;
			}

			std::string line;
			while (std::getline(stream, line)) {
				const std::string trimmed = TrimCopy(line);
				if (trimmed.empty()) {
					continue;
				}
				try {
					const CronJson parsed = ParseJsonWithJson5Fallback(trimmed);
					if (parsed.is_discarded() || !IsFinishedRunLogLine(parsed)) {
						continue;
					}
					runs.push_back(parsed);
				}
				catch (...) {
				}
			}
		}

		return runs;
	}

	void CronStoreService::MigrateLegacyRunsToJsonlIfNeeded() {
		bool hasJsonl = false;
		std::error_code ec;
		if (std::filesystem::exists(m_runsDir)) {
			for (const auto& entry : std::filesystem::directory_iterator(m_runsDir, ec)) {
				if (ec || !entry.is_regular_file()) {
					continue;
				}
				const std::string filename = entry.path().filename().string();
				if (filename.size() >= 6 &&
					filename.compare(filename.size() - 6, 6, ".jsonl") == 0) {
					hasJsonl = true;
					break;
				}
			}
		}

		if (hasJsonl || m_runs.empty()) {
			return;
		}

		for (const auto& runEntry : m_runs) {
			AppendRunEntryToJobLog(runEntry);
		}
		m_runsPersistedCount = m_runs.size();
	}

	void CronStoreService::AppendRunEntryToJobLog(const CronJson& runEntry) {
		if (!runEntry.is_object()) {
			return;
		}

		const std::string jobId = TrimCopy(runEntry.value("jobId", std::string()));
		if (!IsSafeRunLogJobId(jobId)) {
			return;
		}

		std::error_code ec;
		const std::filesystem::path logPath = ResolveRunLogPath(m_jobsPath, jobId);
		std::filesystem::create_directories(logPath.parent_path(), ec);

		CronJson lineEntry = runEntry;
		if (!lineEntry.contains("action") || !lineEntry["action"].is_string()) {
			lineEntry["action"] = "finished";
		}

		std::ofstream stream(logPath, std::ios::binary | std::ios::app);
		if (!stream.is_open()) {
			return;
		}
		stream << lineEntry.dump() << '\n';
		stream.flush();

		PruneRunLogFileToMaxLines(logPath, ResolveMaxRunLogLinesPerJob());
	}

	std::filesystem::file_time_type CronStoreService::ReadRunsDirWriteTime(
		const std::filesystem::path& runsDir) {
		std::filesystem::file_time_type latest{};
		if (!std::filesystem::exists(runsDir)) {
			return latest;
		}

		std::error_code ec;
		latest = ReadLastWriteTime(runsDir);
		for (const auto& entry : std::filesystem::directory_iterator(runsDir, ec)) {
			if (ec || !entry.is_regular_file()) {
				continue;
			}
			const std::filesystem::file_time_type candidate =
				entry.last_write_time(ec);
			if (!ec && candidate > latest) {
				latest = candidate;
			}
		}
		return latest;
	}

	CronJson CronStoreService::LoadArrayFile(
		const std::filesystem::path& path,
		const char* expectedKind) {
		if (!std::filesystem::exists(path)) {
			return CronJson::array();
		}

		std::ifstream stream(path, std::ios::binary);
		if (!stream.is_open()) {
			return CronJson::array();
		}

		try {
			const CronJson parsed = ParseJsonStreamWithJson5Fallback(stream);
			if (IsUsableJsonDocument(parsed)) {
				return ParseArrayPayload(parsed, expectedKind);
			}
		}
		catch (...) {
		}

		const std::filesystem::path backupPath = path.string() + ".bak";
		std::ifstream backup(backupPath, std::ios::binary);
		if (!backup.is_open()) {
			return CronJson::array();
		}

		try {
			const CronJson parsed = ParseJsonStreamWithJson5Fallback(backup);
			if (IsUsableJsonDocument(parsed)) {
				return ParseArrayPayload(parsed, expectedKind);
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
