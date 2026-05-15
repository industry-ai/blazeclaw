#include "pch.h"

#include "CronOpsService.h"

#include "../gateway/GatewayPersistencePaths.h"

#include <algorithm>

namespace blazeclaw::cron {
	namespace {
		std::string ReadStringOrEmpty(const CronJson& value, const char* key) {
			if (!value.contains(key) || value[key].is_null()) {
				return {};
			}
			if (value[key].is_string()) {
				return value[key].get<std::string>();
			}
			if (value[key].is_number_integer()) {
				return std::to_string(value[key].get<std::int64_t>());
			}
			if (value[key].is_number_unsigned()) {
				return std::to_string(value[key].get<std::uint64_t>());
			}
			if (value[key].is_number_float()) {
				return std::to_string(value[key].get<double>());
			}
			if (value[key].is_boolean()) {
				return value[key].get<bool>() ? "true" : "false";
			}
			return {};
		}
	}

	CronOpsService::CronOpsService()
		: m_store(
			gateway::ResolveGatewayStateFilePath("cron.jobs.json"),
			gateway::ResolveGatewayStateFilePath("cron.runs.json")) {
	}

	CronJson CronOpsService::Status(const CronJson& params) {
		(void)params;
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		const std::int64_t nowMs = UtcNowMs();
		SyncDueRunsLocked(nowMs, false);
		const std::int64_t nextWakeAtMs = m_timer.ComputeNextWakeAtMs(m_store.Jobs());
		return {
			{ "enabled", true },
			{ "storePath", m_store.JobsPath().string() },
			{ "jobs", m_store.Jobs().size() },
			{ "nextWakeAtMs", nextWakeAtMs > 0 ? CronJson(nextWakeAtMs) : CronJson(nullptr) }
		};
	}

	CronJson CronOpsService::List(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		SyncDueRunsLocked(UtcNowMs(), false);

		const std::size_t requestedLimit =
			ClampLimit(params.value("limit", 20), 1, 200, 20);
		const std::size_t requestedOffset =
			ClampLimit(params.value("offset", 0), 0, 1'000'000, 0);
		const std::string enabledFilter =
			ToLowerCopy(TrimCopy(params.value("enabled", std::string())));
		const std::string query =
			ToLowerCopy(TrimCopy(params.value("query", std::string())));
		const std::string sortBy =
			TrimCopy(params.value("sortBy", std::string("nextRunAtMs")));
		const std::string sortDir =
			TrimCopy(params.value("sortDir", std::string("asc")));

		std::vector<CronJson> filtered;
		filtered.reserve(m_store.Jobs().size());
		for (const auto& job : m_store.Jobs()) {
			const bool enabled = job.value("enabled", true);
			if (enabledFilter == "enabled" && !enabled) {
				continue;
			}
			if (enabledFilter == "disabled" && enabled) {
				continue;
			}
			if (!query.empty()) {
				const std::string haystack = ToLowerCopy(
					job.value("id", std::string()) + " " +
					job.value("name", std::string()) + " " +
					job.value("description", std::string()));
				if (haystack.find(query) == std::string::npos) {
					continue;
				}
			}
			filtered.push_back(job);
		}

		std::sort(
			filtered.begin(),
			filtered.end(),
			[sortBy, sortDir](const CronJson& left, const CronJson& right) {
				const bool ascending = ToLowerCopy(sortDir) != "desc";
				if (sortBy == "name") {
					const auto lv = left.value("name", std::string());
					const auto rv = right.value("name", std::string());
					return ascending ? lv < rv : lv > rv;
				}
				if (sortBy == "updatedAtMs") {
					const auto lv = left.value("updatedAtMs", static_cast<std::int64_t>(0));
					const auto rv = right.value("updatedAtMs", static_cast<std::int64_t>(0));
					return ascending ? lv < rv : lv > rv;
				}
				const auto leftState = left.value("state", CronJson::object());
				const auto rightState = right.value("state", CronJson::object());
				const auto lv = leftState.value("nextRunAtMs", static_cast<std::int64_t>(0));
				const auto rv = rightState.value("nextRunAtMs", static_cast<std::int64_t>(0));
				return ascending ? lv < rv : lv > rv;
			});

		const std::size_t total = filtered.size();
		const std::size_t offset = (std::min)(requestedOffset, total);
		const std::size_t end = (std::min)(offset + requestedLimit, total);

		CronJson jobsPage = CronJson::array();
		for (std::size_t index = offset; index < end; ++index) {
			jobsPage.push_back(filtered[index]);
		}

		const bool hasMore = end < total;
		return {
			{ "jobs", jobsPage },
			{ "total", total },
			{ "limit", requestedLimit },
			{ "offset", offset },
			{ "nextOffset", hasMore ? CronJson(end) : CronJson(nullptr) },
			{ "hasMore", hasMore }
		};
	}

	CronJson CronOpsService::Add(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		const std::int64_t nowMs = UtcNowMs();

		CronJson job = CronNormalize::NormalizeAddInput(params);
		job["id"] =
			"cron-" + std::to_string(nowMs) + "-" + std::to_string(++m_idCounter);
		job["createdAtMs"] = nowMs;
		job["updatedAtMs"] = nowMs;

		job["state"]["nextRunAtMs"] =
			m_timer.ComputeNextRunAtMs(job, nowMs).value_or(0);
		if (job["state"]["nextRunAtMs"].is_number_integer() &&
			job["state"]["nextRunAtMs"].get<std::int64_t>() <= 0) {
			job["state"]["nextRunAtMs"] = nullptr;
		}

		m_store.Jobs().push_back(job);
		m_store.SaveJobs();
		return job;
	}

	CronJson CronOpsService::Update(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();

		const std::string id = CronNormalize::ResolveCronId(params);
		if (id.empty()) {
			throw std::invalid_argument("missing `id` or `jobId`");
		}
		if (!params.contains("patch") || !params["patch"].is_object()) {
			throw std::invalid_argument("`patch` must be an object");
		}

		CronJson* job = FindJobByIdLocked(id);
		if (job == nullptr) {
			throw std::invalid_argument("unknown cron job id");
		}

		CronNormalize::ApplyPatch(*job, params["patch"]);
		const std::int64_t nowMs = UtcNowMs();
		(*job)["updatedAtMs"] = nowMs;
		(*job)["state"]["nextRunAtMs"] = (*job).value("enabled", true)
			? CronJson(m_timer.ComputeNextRunAtMs(*job, nowMs).value_or(0))
			: CronJson(nullptr);
		if ((*job)["state"]["nextRunAtMs"].is_number_integer() &&
			(*job)["state"]["nextRunAtMs"].get<std::int64_t>() <= 0) {
			(*job)["state"]["nextRunAtMs"] = nullptr;
		}

		m_store.SaveJobs();
		return *job;
	}

	CronJson CronOpsService::Remove(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();

		const std::string id = CronNormalize::ResolveCronId(params);
		if (id.empty()) {
			throw std::invalid_argument("missing `id` or `jobId`");
		}

		const std::size_t before = m_store.Jobs().size();
		m_store.Jobs().erase(
			std::remove_if(
				m_store.Jobs().begin(),
				m_store.Jobs().end(),
				[id](const CronJson& job) {
					return job.value("id", std::string()) == id;
				}),
			m_store.Jobs().end());

		const bool removed = m_store.Jobs().size() != before;
		if (removed) {
			m_store.SaveJobs();
		}

		return {
			{ "ok", true },
			{ "removed", removed }
		};
	}

	CronJson CronOpsService::Run(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		const std::string id = CronNormalize::ResolveCronId(params);
		if (id.empty()) {
			throw std::invalid_argument("missing `id` or `jobId`");
		}

		CronJson* job = FindJobByIdLocked(id);
		if (job == nullptr) {
			throw std::invalid_argument("unknown cron job id");
		}

		const std::string mode =
			ToLowerCopy(TrimCopy(params.value("mode", std::string("force"))));
		if (mode != "force" && mode != "due") {
			throw std::invalid_argument("`mode` must be `due` or `force`");
		}
		const std::int64_t nowMs = UtcNowMs();
		const auto nextRunAtMs = TryReadInt64Field((*job)["state"], "nextRunAtMs");
		if (mode == "due" &&
			(!nextRunAtMs.has_value() || nextRunAtMs.value() > nowMs)) {
			return {
				{ "runId", BuildCronRunId(nowMs) },
				{ "started", false },
				{ "reason", "not_due" },
				{ "cronId", id },
				{ "mode", "due" },
				{ "queuedAtMs", nowMs }
			};
		}

		if (mode == "force" ||
			!nextRunAtMs.has_value() ||
			nextRunAtMs.value() > nowMs) {
			(*job)["state"]["nextRunAtMs"] = nowMs;
		}
		SyncDueRunsLocked(nowMs, false);

		return {
			{ "runId", BuildCronRunId(nowMs) },
			{ "started", true },
			{ "cronId", id },
			{ "mode", mode == "due" ? "due" : "force" },
			{ "queuedAtMs", nowMs }
		};
	}

	CronJson CronOpsService::Runs(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();

		const std::size_t requestedLimit =
			ClampLimit(params.value("limit", 20), 1, 200, 20);
		const std::size_t requestedOffset =
			ClampLimit(params.value("offset", 0), 0, 1'000'000, 0);
		const std::string scope =
			ToLowerCopy(TrimCopy(params.value("scope", std::string("all"))));
		const std::string requestedId = CronNormalize::ResolveCronId(params);
		const std::string statusFilter =
			ToLowerCopy(TrimCopy(params.value("status", std::string("all"))));
		const std::string query =
			ToLowerCopy(TrimCopy(params.value("query", std::string())));
		const std::string sortDir =
			ToLowerCopy(TrimCopy(params.value("sortDir", std::string("desc"))));

		std::vector<CronJson> filtered;
		filtered.reserve(m_store.Runs().size());
		for (const auto& entry : m_store.Runs()) {
			const std::string jobId = ReadStringOrEmpty(entry, "jobId");
			const std::string status =
				ToLowerCopy(ReadStringOrEmpty(entry, "status"));

			if (scope == "job" && !requestedId.empty() && jobId != requestedId) {
				continue;
			}
			if (statusFilter != "" && statusFilter != "all" && status != statusFilter) {
				continue;
			}
			if (!query.empty()) {
				const std::string haystack = ToLowerCopy(
					jobId + " " +
					ReadStringOrEmpty(entry, "status") + " " +
					ReadStringOrEmpty(entry, "jobName") + " " +
					ReadStringOrEmpty(entry, "error"));
				if (haystack.find(query) == std::string::npos) {
					continue;
				}
			}
			filtered.push_back(entry);
		}

		std::sort(
			filtered.begin(),
			filtered.end(),
			[sortDir](const CronJson& left, const CronJson& right) {
				const std::int64_t leftTs = left.value("ts", static_cast<std::int64_t>(0));
				const std::int64_t rightTs = right.value("ts", static_cast<std::int64_t>(0));
				if (sortDir == "asc") {
					return leftTs < rightTs;
				}
				return leftTs > rightTs;
			});

		const std::size_t total = filtered.size();
		const std::size_t offset = (std::min)(requestedOffset, total);
		const std::size_t end = (std::min)(offset + requestedLimit, total);

		CronJson entries = CronJson::array();
		for (std::size_t index = offset; index < end; ++index) {
			entries.push_back(filtered[index]);
		}

		const bool hasMore = end < total;
		return {
			{ "entries", entries },
			{ "total", total },
			{ "limit", requestedLimit },
			{ "offset", offset },
			{ "nextOffset", hasMore ? CronJson(end) : CronJson(nullptr) },
			{ "hasMore", hasMore }
		};
	}

	CronJson CronOpsService::Wake(const CronJson& params) {
		const std::string mode =
			ToLowerCopy(TrimCopy(params.value("mode", std::string(kWakeModeNow))));
		if (!IsWakeModeValid(mode)) {
			throw std::invalid_argument("`mode` must be `now` or `next-heartbeat`");
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		const std::int64_t nowMs = UtcNowMs();
		if (mode == kWakeModeNow) {
			SyncDueRunsLocked(nowMs, false);
		}
		else {
			SyncDueRunsLocked(nowMs, false);
		}

		return {
			{ "ok", true },
			{ "mode", mode },
			{ "text", params.value("text", std::string()) },
			{ "requestedAtMs", nowMs }
		};
	}

	std::size_t CronOpsService::ClampLimit(
		const CronJson& value,
		const std::size_t min,
		const std::size_t max,
		const std::size_t fallback) {
		if (!value.is_number()) {
			return fallback;
		}
		const auto parsed = static_cast<std::size_t>(value.get<std::int64_t>());
		return (std::max)(min, (std::min)(max, parsed));
	}

	CronJson* CronOpsService::FindJobByIdLocked(const std::string& id) {
		for (auto& job : m_store.Jobs()) {
			if (job.value("id", std::string()) == id) {
				return &job;
			}
		}
		return nullptr;
	}

	void CronOpsService::EnsureLoadedLocked() {
		m_store.EnsureLoaded();
		if (m_idCounter == 0) {
			m_idCounter = static_cast<std::uint64_t>(m_store.Jobs().size() + m_store.Runs().size());
		}
	}

	void CronOpsService::RunStartupCatchupLocked() {
		if (m_startupCatchupDone) {
			return;
		}

		m_startupCatchupDone = true;
		SyncDueRunsLocked(UtcNowMs(), false);
	}

	void CronOpsService::SyncDueRunsLocked(
		const std::int64_t nowMs,
		const bool forceRunDue) {
		bool changed = m_timer.RecomputeSchedules(m_store.Jobs(), nowMs);
		std::size_t executedTotal = 0;
		std::size_t loops = 0;

		while (loops < m_maxCatchupRunsPerSync) {
			const std::size_t executed =
				m_timer.PumpDueRuns(m_store.Jobs(), m_store.Runs(), nowMs, forceRunDue);
			if (executed == 0) {
				break;
			}

			executedTotal += executed;
			changed = true;
			m_timer.RecomputeSchedules(m_store.Jobs(), nowMs);
			++loops;
		}

		if (executedTotal > 0) {
			m_store.SaveRuns();
		}
		if (changed) {
			m_store.SaveJobs();
		}

		m_lastSyncAtMs = nowMs;
	}

	CronOpsService& GetCronOpsService() {
		static CronOpsService service;
		return service;
	}

} // namespace blazeclaw::cron
