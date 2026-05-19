#include "pch.h"

#include "CronOpsService.h"

#include "../gateway/GatewayPersistencePaths.h"

#include <algorithm>
#include <utility>
#include <unordered_set>

namespace blazeclaw::cron {
	namespace {
			inline constexpr const char* kTaskLedgerStatusOk = "ok";
			inline constexpr const char* kTaskLedgerStatusFailed = "failed";
			inline constexpr const char* kTaskLedgerStatusTimedOut = "timed_out";
			inline constexpr const char* kTaskLedgerStatusAborted = "aborted";
			inline constexpr const char* kTaskLedgerStatusSkipped = "skipped";

			inline constexpr const char* kTaskLedgerDispositionDispatched = "dispatched";
			inline constexpr const char* kTaskLedgerDispositionFailed = "failed";
			inline constexpr const char* kTaskLedgerDispositionTimedOut = "timed_out";
			inline constexpr const char* kTaskLedgerDispositionAborted = "aborted";
			inline constexpr const char* kTaskLedgerDispositionSkipped = "skipped";
			inline constexpr const char* kTaskLedgerDispositionSuppressed = "suppressed";
			inline constexpr const char* kTaskLedgerDispositionNotDelivered = "not_delivered";

		CronJson BuildManualLifecycleEntry(
			const std::string& runId,
			const std::string& jobId,
			const std::string& mode,
			const std::string& action,
			const std::string& status,
			const std::string& lifecycleState,
			const std::string& reason,
			const std::string& summary,
			const std::int64_t ts,
			const std::int64_t queuedAtMs,
			const std::optional<std::int64_t>& startedAtMs,
			const std::optional<std::int64_t>& endedAtMs) {
			CronJson entry = {
				{ "ts", ts },
				{ "jobId", jobId },
				{ "runId", runId },
				{ "manual", true },
				{ "mode", mode },
				{ "action", action },
				{ "status", status },
				{ "lifecycleState", lifecycleState },
				{ "summary", summary },
				{ "reason", reason },
				{ "queuedAtMs", queuedAtMs },
				{ "startedAtMs", startedAtMs.has_value() ? CronJson(startedAtMs.value()) : CronJson(nullptr) },
				{ "endedAtMs", endedAtMs.has_value() ? CronJson(endedAtMs.value()) : CronJson(nullptr) }
			};

			if (status == "queued" || status == "running") {
				entry["deliveryStatus"] = "not-requested";
				entry["error"] = CronJson(nullptr);
			}

			entry["taskLedgerRuntime"] = "cron";
			if (action == "queued") {
				entry["taskLedgerPhase"] = "queued";
			}
			else if (action == "started") {
				entry["taskLedgerPhase"] = "active";
			}
			else {
				entry["taskLedgerPhase"] = "terminal";
			}
			entry["taskLedgerStatus"] = status;

			return entry;
		}

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

		std::string MapTerminalStatus(const CronJson& finishedRun) {
			if (finishedRun.value("timedOut", false)) {
				return "timed_out";
			}

			if (finishedRun.value("aborted", false)) {
				return "aborted";
			}

			const std::string sourceStatus =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "status"));
			const std::string sourceErrorCategory =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "errorCategory"));
			const std::string sourceDeliveryStatus =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "deliveryStatus"));
			if (sourceErrorCategory == "timeout") {
				return kTaskLedgerStatusTimedOut;
			}
			if (sourceErrorCategory == "aborted") {
				return kTaskLedgerStatusAborted;
			}
			const bool retryScheduled =
				finishedRun.contains("retryScheduled") &&
				finishedRun["retryScheduled"].is_boolean() &&
				finishedRun["retryScheduled"].get<bool>();
			if (sourceStatus == "ok") {
				return kTaskLedgerStatusOk;
			}
			if (sourceStatus == "queued") {
				return "queued";
			}
			if (sourceStatus == "running") {
				return "running";
			}
			if (sourceStatus == "error") {
				if (sourceErrorCategory == "timeout") {
					return kTaskLedgerStatusTimedOut;
				}
				if (sourceDeliveryStatus == "suppressed") {
					return kTaskLedgerStatusSkipped;
				}
				if (sourceDeliveryStatus == "not-delivered" && !retryScheduled) {
					return kTaskLedgerStatusFailed;
				}
				return kTaskLedgerStatusFailed;
			}
			if (sourceStatus == "failed") {
				return kTaskLedgerStatusFailed;
			}
			if (sourceStatus == "timed_out") {
				return kTaskLedgerStatusTimedOut;
			}
			if (sourceStatus == "aborted") {
				return kTaskLedgerStatusAborted;
			}
			if (sourceStatus == "skipped") {
				return kTaskLedgerStatusSkipped;
			}

			return kTaskLedgerStatusSkipped;
		}

		std::string MapTerminalDisposition(const CronJson& finishedRun) {
			const std::string explicitTaskLedgerDisposition =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "taskLedgerDisposition"));
			if (explicitTaskLedgerDisposition == "already_running" ||
				explicitTaskLedgerDisposition == "not_due" ||
				explicitTaskLedgerDisposition == "unknown_job" ||
				explicitTaskLedgerDisposition == "missing_terminal_run") {
				return explicitTaskLedgerDisposition;
			}

			if (finishedRun.value("timedOut", false)) {
				return kTaskLedgerDispositionTimedOut;
			}

			if (finishedRun.value("aborted", false)) {
				return kTaskLedgerDispositionAborted;
			}

			const std::string sourceStatus =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "status"));
			const std::string sourceErrorCategory =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "errorCategory"));
			const std::string sourceDeliveryStatus =
				ToLowerCopy(ReadStringOrEmpty(finishedRun, "deliveryStatus"));
			if (sourceErrorCategory == "timeout") {
				return kTaskLedgerDispositionTimedOut;
			}
			if (sourceErrorCategory == "aborted") {
				return kTaskLedgerDispositionAborted;
			}
			const bool retryScheduled =
				finishedRun.contains("retryScheduled") &&
				finishedRun["retryScheduled"].is_boolean() &&
				finishedRun["retryScheduled"].get<bool>();
			if (sourceStatus == "ok") {
				if (sourceDeliveryStatus == "suppressed") {
					return kTaskLedgerDispositionSuppressed;
				}
				return kTaskLedgerDispositionDispatched;
			}
			if (sourceStatus == "error" ||
				sourceStatus == "failed") {
				if (sourceDeliveryStatus == "suppressed") {
					return kTaskLedgerDispositionSuppressed;
				}
				if (sourceDeliveryStatus == "not-delivered" && !retryScheduled) {
					return kTaskLedgerDispositionNotDelivered;
				}
				return kTaskLedgerDispositionFailed;
			}
			if (sourceStatus == "timed_out") {
				return kTaskLedgerDispositionTimedOut;
			}
			if (sourceStatus == "aborted") {
				return kTaskLedgerDispositionAborted;
			}
			if (sourceStatus == "skipped") {
				return kTaskLedgerDispositionSkipped;
			}

			return kTaskLedgerDispositionDispatched;
		}

		bool IsFailureTaskLedgerStatus(const std::string& statusRaw) {
			const std::string status = ToLowerCopy(TrimCopy(statusRaw));
			return status == "error" ||
				status == "failed" ||
				status == "timed_out" ||
				status == "aborted";
		}

		CronJson BuildTaskLedgerHookPayload(const CronJson& runEntry) {
			CronJson payload = {
				{ "runtime", "cron" },
				{ "runId", ReadStringOrEmpty(runEntry, "runId") },
				{ "jobId", ReadStringOrEmpty(runEntry, "jobId") },
				{ "status", ReadStringOrEmpty(runEntry, "status") },
				{ "summary", ReadStringOrEmpty(runEntry, "summary") },
				{ "action", ReadStringOrEmpty(runEntry, "action") },
				{ "disposition", ReadStringOrEmpty(runEntry, "taskLedgerDisposition") }
			};

			if (runEntry.contains("taskLedgerStatus")) {
				payload["taskLedgerStatus"] = runEntry["taskLedgerStatus"];
			}
			if (runEntry.contains("taskLedgerPhase")) {
				payload["taskLedgerPhase"] = runEntry["taskLedgerPhase"];
			}
			if (runEntry.contains("taskLedgerRuntime")) {
				payload["taskLedgerRuntime"] = runEntry["taskLedgerRuntime"];
			}
			if (runEntry.contains("taskLedgerTerminal")) {
				payload["taskLedgerTerminal"] = runEntry["taskLedgerTerminal"];
			}
		if (runEntry.contains("queuedAtMs")) {
			payload["queuedAtMs"] = runEntry["queuedAtMs"];
		}
		if (runEntry.contains("startedAtMs")) {
			payload["startedAtMs"] = runEntry["startedAtMs"];
		}
		if (runEntry.contains("endedAtMs")) {
			payload["endedAtMs"] = runEntry["endedAtMs"];
		}

			if (runEntry.contains("error")) {
				payload["error"] = runEntry["error"];
			}
			if (runEntry.contains("errorCategory")) {
				payload["errorCategory"] = runEntry["errorCategory"];
			}
			if (runEntry.contains("deliveryStatus")) {
				payload["deliveryStatus"] = runEntry["deliveryStatus"];
			}
			if (runEntry.contains("deliveryMode")) {
				payload["deliveryMode"] = runEntry["deliveryMode"];
			}
			if (runEntry.contains("deliveryTarget")) {
				payload["deliveryTarget"] = runEntry["deliveryTarget"];
			}
			if (runEntry.contains("deliveryAttempted")) {
				payload["deliveryAttempted"] = runEntry["deliveryAttempted"];
			}
			if (runEntry.contains("deliveryHttpStatus")) {
				payload["deliveryHttpStatus"] = runEntry["deliveryHttpStatus"];
			}
			if (runEntry.contains("deliveryError")) {
				payload["deliveryError"] = runEntry["deliveryError"];
			}
			if (runEntry.contains("deliveryChannel")) {
				payload["deliveryChannel"] = runEntry["deliveryChannel"];
			}
			if (runEntry.contains("deliveryAccountId")) {
				payload["deliveryAccountId"] = runEntry["deliveryAccountId"];
			}
			if (runEntry.contains("failureDestinationStatus")) {
				payload["failureDestinationStatus"] = runEntry["failureDestinationStatus"];
			}
			if (runEntry.contains("failureDestinationMode")) {
				payload["failureDestinationMode"] = runEntry["failureDestinationMode"];
			}
			if (runEntry.contains("failureDestinationTarget")) {
				payload["failureDestinationTarget"] = runEntry["failureDestinationTarget"];
			}
			if (runEntry.contains("failureDestinationAttempted")) {
				payload["failureDestinationAttempted"] =
					runEntry["failureDestinationAttempted"];
			}
			if (runEntry.contains("failureDestinationHttpStatus")) {
				payload["failureDestinationHttpStatus"] =
					runEntry["failureDestinationHttpStatus"];
			}
			if (runEntry.contains("failureDestinationChannel")) {
				payload["failureDestinationChannel"] =
					runEntry["failureDestinationChannel"];
			}
			if (runEntry.contains("failureDestinationAccountId")) {
				payload["failureDestinationAccountId"] =
					runEntry["failureDestinationAccountId"];
			}
			if (runEntry.contains("failureDestinationError")) {
				payload["failureDestinationError"] = runEntry["failureDestinationError"];
			}
			if (runEntry.contains("sessionId")) {
				payload["sessionId"] = runEntry["sessionId"];
			}
			if (runEntry.contains("sessionKey")) {
				payload["sessionKey"] = runEntry["sessionKey"];
			}
			if (runEntry.contains("model")) {
				payload["model"] = runEntry["model"];
			}
			if (runEntry.contains("provider")) {
				payload["provider"] = runEntry["provider"];
			}
			if (runEntry.contains("usage")) {
				payload["usage"] = runEntry["usage"];
			}
			if (runEntry.contains("retryAttempt")) {
				payload["retryAttempt"] = runEntry["retryAttempt"];
			}
			if (runEntry.contains("retryScheduled")) {
				payload["retryScheduled"] = runEntry["retryScheduled"];
			}
			if (runEntry.contains("retryScheduledAtMs")) {
				payload["retryScheduledAtMs"] = runEntry["retryScheduledAtMs"];
			}
			if (runEntry.contains("nextRunAtMs")) {
				payload["nextRunAtMs"] = runEntry["nextRunAtMs"];
			}
			if (runEntry.contains("heartbeatBusyAttempts")) {
				payload["heartbeatBusyAttempts"] = runEntry["heartbeatBusyAttempts"];
			}
			if (runEntry.contains("heartbeatFallbackWakeRequested")) {
				payload["heartbeatFallbackWakeRequested"] =
					runEntry["heartbeatFallbackWakeRequested"];
			}
			if (runEntry.contains("heartbeatFallbackWakeRequestedAtMs")) {
				payload["heartbeatFallbackWakeRequestedAtMs"] =
					runEntry["heartbeatFallbackWakeRequestedAtMs"];
			}
			if (runEntry.contains("failureAlertTriggered")) {
				payload["failureAlertTriggered"] = runEntry["failureAlertTriggered"];
			}
			if (runEntry.contains("failureAlertSuppressed")) {
				payload["failureAlertSuppressed"] = runEntry["failureAlertSuppressed"];
			}
			if (runEntry.contains("failureAlertSuppressedReason")) {
				payload["failureAlertSuppressedReason"] =
					runEntry["failureAlertSuppressedReason"];
			}
			if (runEntry.contains("failureAlertMode")) {
				payload["failureAlertMode"] = runEntry["failureAlertMode"];
			}
			if (runEntry.contains("failureAlertTarget")) {
				payload["failureAlertTarget"] = runEntry["failureAlertTarget"];
			}
			if (runEntry.contains("failureAlertChannel")) {
				payload["failureAlertChannel"] = runEntry["failureAlertChannel"];
			}
			if (runEntry.contains("failureAlertAccountId")) {
				payload["failureAlertAccountId"] = runEntry["failureAlertAccountId"];
			}
			if (runEntry.contains("failureAlertAtMs")) {
				payload["failureAlertAtMs"] = runEntry["failureAlertAtMs"];
			}

			return payload;
		}
	}

	CronOpsService::CronOpsService()
		: m_store(
			gateway::ResolveGatewayStateFilePath("cron.jobs.json"),
			gateway::ResolveGatewayStateFilePath("cron.runs.json")) {
	}

	CronOpsService::~CronOpsService() {
		StopBackgroundScheduler();
	}

	CronJson CronOpsService::Status(const CronJson& params) {
		(void)params;
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		const std::int64_t nowMs = UtcNowMs();
		RefreshSchedulesOnlyLocked(nowMs);
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
		RefreshSchedulesOnlyLocked(UtcNowMs());

		const std::size_t requestedLimit =
			ClampLimit(params.value("limit", 20), 1, 200, 20);
		const std::size_t requestedOffset =
			ClampLimit(params.value("offset", 0), 0, 1'000'000, 0);
		const bool includeDisabled = params.value("includeDisabled", false);
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
			const std::string resolvedEnabledFilter =
				enabledFilter == "all" || enabledFilter == "enabled" || enabledFilter == "disabled"
				? enabledFilter
				: (includeDisabled ? "all" : "enabled");
			if (resolvedEnabledFilter == "enabled" && !enabled) {
				continue;
			}
			if (resolvedEnabledFilter == "disabled" && enabled) {
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
		const std::int64_t nowMs = UtcNowMs();
		ProcessManualRunQueueLocked(nowMs);
		const std::string id = CronNormalize::ResolveCronId(params);
		if (id.empty()) {
			throw std::invalid_argument("missing `id` or `jobId`");
		}

		CronJson* job = FindJobByIdLocked(id);
		if (job == nullptr) {
			throw std::invalid_argument("unknown cron job id");
		}
		if ((*job).contains("state") &&
			(*job)["state"].is_object() &&
			TryReadInt64Field((*job)["state"], "runningAtMs").has_value()) {
			const std::int64_t alreadyRunningAtMs = nowMs;
			const std::string requestedMode =
				ToLowerCopy(TrimCopy(params.value("mode", std::string("force"))));
			const std::string runId =
				"manual:" + id + ":" + std::to_string(alreadyRunningAtMs) + ":" + std::to_string(++m_manualRunCounter);

			m_store.Runs().push_back(BuildManualLifecycleEntry(
				runId,
				id,
				requestedMode,
				"finished",
				"skipped",
				"terminal",
				"already_running",
				"manual run skipped because cron job is already running",
				alreadyRunningAtMs,
				alreadyRunningAtMs,
				std::nullopt,
				alreadyRunningAtMs));
			m_store.Runs().back()["taskLedgerDisposition"] = "already_running";
			m_store.Runs().back()["taskLedgerTerminal"] = true;
			EmitTaskLedgerTerminalHook(m_store.Runs().back());
			m_store.SaveRuns();

			return {
				{ "ok", true },
				{ "runId", runId },
				{ "enqueued", false },
				{ "started", false },
				{ "reason", "already_running" },
				{ "cronId", id },
				{ "mode", requestedMode },
				{ "queuedAtMs", alreadyRunningAtMs },
				{ "runState", "terminal" }
			};
		}

		const std::string mode =
			ToLowerCopy(TrimCopy(params.value("mode", std::string("force"))));
		if (mode != "force" && mode != "due") {
			throw std::invalid_argument("`mode` must be `due` or `force`");
		}
		const auto nextRunAtMs = TryReadInt64Field((*job)["state"], "nextRunAtMs");
		if (mode == "due" &&
			(!nextRunAtMs.has_value() || nextRunAtMs.value() > nowMs)) {
			const std::string runId =
				"manual:" + id + ":" + std::to_string(nowMs) + ":" + std::to_string(++m_manualRunCounter);

			m_store.Runs().push_back(BuildManualLifecycleEntry(
				runId,
				id,
				"due",
				"finished",
				"skipped",
				"terminal",
				"not_due",
				"manual run skipped because cron job is not due",
				nowMs,
				nowMs,
				std::nullopt,
				nowMs));
			m_store.Runs().back()["taskLedgerDisposition"] = "not_due";
			m_store.Runs().back()["taskLedgerTerminal"] = true;
			EmitTaskLedgerTerminalHook(m_store.Runs().back());
			m_store.SaveRuns();

			return {
				{ "ok", true },
				{ "runId", runId },
				{ "enqueued", false },
				{ "started", false },
				{ "reason", "not_due" },
				{ "cronId", id },
				{ "mode", "due" },
				{ "queuedAtMs", nowMs },
				{ "runState", "terminal" }
			};
		}

		ManualRunRequest request{};
		request.jobId = id;
		request.mode = mode;
		request.queuedAtMs = nowMs;
		request.runId =
			"manual:" + id + ":" + std::to_string(nowMs) + ":" + std::to_string(++m_manualRunCounter);
		m_manualRunQueue.push_back(request);
		m_store.Runs().push_back(BuildManualLifecycleEntry(
			request.runId,
			id,
			mode == "due" ? "due" : "force",
			"queued",
			"queued",
			"queued",
			"queued",
			"manual run queued for scheduler dispatch",
			nowMs,
			nowMs,
			std::nullopt,
			std::nullopt));
		m_store.Runs().back()["taskLedgerDisposition"] = "queued";
		m_store.Runs().back()["taskLedgerTerminal"] = false;
		m_store.SaveRuns();

		m_backgroundCv.notify_all();

		return {
			{ "ok", true },
			{ "runId", request.runId },
			{ "enqueued", true },
			{ "started", false },
			{ "reason", "queued" },
			{ "cronId", id },
			{ "mode", mode == "due" ? "due" : "force" },
			{ "queuedAtMs", nowMs },
			{ "queueDepth", m_manualRunQueue.size() },
			{ "runState", "queued" }
		};
	}

	CronJson CronOpsService::Runs(const CronJson& params) {
		std::lock_guard<std::mutex> lock(m_mutex);
		EnsureLoadedLocked();
		RunStartupCatchupLocked();
		RefreshSchedulesOnlyLocked(UtcNowMs());

		const std::size_t requestedLimit =
			ClampLimit(params.value("limit", 20), 1, 200, 20);
		const std::size_t requestedOffset =
			ClampLimit(params.value("offset", 0), 0, 1'000'000, 0);
		const std::string scope =
			ToLowerCopy(TrimCopy(params.value("scope", std::string("all"))));
		const std::string requestedId = CronNormalize::ResolveCronId(params);

		std::unordered_set<std::string> statusFilters;
		if (params.contains("statuses") && params["statuses"].is_array()) {
			for (const auto& item : params["statuses"]) {
				if (!item.is_string()) {
					continue;
				}

				const std::string value = ToLowerCopy(TrimCopy(item.get<std::string>()));
				if (value == "ok" ||
					value == "error" ||
					value == "skipped" ||
					value == "queued" ||
					value == "running" ||
					value == "failed" ||
					value == "timed_out" ||
					value == "aborted") {
					statusFilters.insert(value);
				}
			}
		}

		const std::string statusFilter =
			ToLowerCopy(TrimCopy(params.value("status", std::string("all"))));
		if (statusFilter == "ok" || statusFilter == "error" || statusFilter == "skipped") {
			statusFilters.insert(statusFilter);
		}
		if (statusFilter == "queued" ||
			statusFilter == "running" ||
			statusFilter == "failed" ||
			statusFilter == "timed_out" ||
			statusFilter == "aborted") {
			statusFilters.insert(statusFilter);
		}

		std::unordered_set<std::string> deliveryStatusFilters;
		if (params.contains("deliveryStatuses") && params["deliveryStatuses"].is_array()) {
			for (const auto& item : params["deliveryStatuses"]) {
				if (!item.is_string()) {
					continue;
				}

				const std::string value = ToLowerCopy(TrimCopy(item.get<std::string>()));
				if (value == "not-requested" ||
					value == "delivered" ||
					value == "not-delivered" ||
					value == "unknown" ||
					value == "suppressed") {
					deliveryStatusFilters.insert(value);
				}
			}
		}

		const std::string deliveryStatusFilter =
			ToLowerCopy(TrimCopy(params.value("deliveryStatus", std::string())));
		if (deliveryStatusFilter == "not-requested" ||
			deliveryStatusFilter == "delivered" ||
			deliveryStatusFilter == "not-delivered" ||
			deliveryStatusFilter == "unknown" ||
			deliveryStatusFilter == "suppressed") {
			deliveryStatusFilters.insert(deliveryStatusFilter);
		}

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
			const std::string deliveryStatus =
				ToLowerCopy(ReadStringOrEmpty(entry, "deliveryStatus"));

			if (scope == "job" && !requestedId.empty() && jobId != requestedId) {
				continue;
			}
			if (!statusFilters.empty() && statusFilters.find(status) == statusFilters.end()) {
				continue;
			}
			if (!deliveryStatusFilters.empty() &&
				deliveryStatusFilters.find(deliveryStatus) == deliveryStatusFilters.end()) {
				continue;
			}
			if (!query.empty()) {
				const std::string haystack = ToLowerCopy(
					jobId + " " +
					ReadStringOrEmpty(entry, "status") + " " +
					ReadStringOrEmpty(entry, "deliveryStatus") + " " +
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
			ProcessManualRunQueueLocked(nowMs);
			SyncDueRunsLocked(nowMs, false);
		}
		else {
			RefreshSchedulesOnlyLocked(nowMs);
		}

		return {
			{ "ok", true },
			{ "mode", mode },
			{ "text", params.value("text", std::string()) },
			{ "requestedAtMs", nowMs }
		};
	}

	void CronOpsService::SetRuntimeExecutionAdapters(
		CronRuntimeExecutionAdapters adapters) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_timer.SetRuntimeExecutionAdapters(std::move(adapters));
	}

	void CronOpsService::SetTaskLedgerHooks(TaskLedgerHooks hooks) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_taskLedgerHooks = std::move(hooks);
	}

	void CronOpsService::StartBackgroundScheduler() {
		std::lock_guard<std::mutex> lock(m_backgroundMutex);
		if (m_backgroundStarted) {
			return;
		}

		m_backgroundStopRequested = false;
		m_backgroundThread = std::thread([this]() {
			BackgroundSchedulerLoop();
			});
		m_backgroundStarted = true;
	}

	void CronOpsService::StopBackgroundScheduler() {
		{
			std::lock_guard<std::mutex> lock(m_backgroundMutex);
			if (!m_backgroundStarted) {
				return;
			}
			m_backgroundStopRequested = true;
		}

		m_backgroundCv.notify_all();
		if (m_backgroundThread.joinable()) {
			m_backgroundThread.join();
		}

		std::lock_guard<std::mutex> lock(m_backgroundMutex);
		m_backgroundStarted = false;
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

	void CronOpsService::BackgroundSchedulerLoop() {
		while (true) {
			{
				std::unique_lock<std::mutex> lock(m_backgroundMutex);
				if (m_backgroundStopRequested) {
					break;
				}
			}

			std::int64_t nowMs = UtcNowMs();
			std::int64_t nextWakeAtMs = 0;

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();
				RunStartupCatchupLocked();
				ProcessManualRunQueueLocked(nowMs);
				SyncDueRunsLocked(nowMs, false);
				nextWakeAtMs = m_timer.ComputeNextWakeAtMs(m_store.Jobs());
			}

			nowMs = UtcNowMs();
			std::int64_t waitMs = 60'000;
			if (nextWakeAtMs > 0) {
				waitMs = (std::max)(static_cast<std::int64_t>(1000), nextWakeAtMs - nowMs);
				waitMs = (std::min)(waitMs, static_cast<std::int64_t>(60'000));
			}

			std::unique_lock<std::mutex> lock(m_backgroundMutex);
			m_backgroundCv.wait_for(
				lock,
				std::chrono::milliseconds(waitMs),
				[this]() {
					return m_backgroundStopRequested;
				});
			if (m_backgroundStopRequested) {
				break;
			}
		}
	}

	void CronOpsService::ProcessManualRunQueueLocked(const std::int64_t nowMs) {
		if (m_manualRunQueue.empty()) {
			return;
		}

		bool jobsChanged = false;
		bool runsChanged = false;
		while (!m_manualRunQueue.empty()) {
			const ManualRunRequest request = m_manualRunQueue.front();
			m_manualRunQueue.pop_front();

			const std::string normalizedMode =
				request.mode == "due" ? "due" : "force";

			CronJson* job = FindJobByIdLocked(request.jobId);
			if (job == nullptr) {
				m_store.Runs().push_back(BuildManualLifecycleEntry(
					request.runId,
					request.jobId,
					normalizedMode,
					"finished",
					"skipped",
					"terminal",
					"unknown_job",
					"manual run dropped because cron job no longer exists",
					nowMs,
					request.queuedAtMs,
					std::nullopt,
					nowMs));
				m_store.Runs().back()["taskLedgerDisposition"] = "unknown_job";
				m_store.Runs().back()["taskLedgerTerminal"] = true;
				EmitTaskLedgerTerminalHook(m_store.Runs().back());
				runsChanged = true;
				continue;
			}

			if ((*job).contains("state") &&
				(*job)["state"].is_object() &&
				TryReadInt64Field((*job)["state"], "runningAtMs").has_value()) {
				m_store.Runs().push_back(BuildManualLifecycleEntry(
					request.runId,
					request.jobId,
					normalizedMode,
					"finished",
					"skipped",
					"terminal",
					"already_running",
					"manual run skipped because cron job is already running",
					nowMs,
					request.queuedAtMs,
					std::nullopt,
					nowMs));
				m_store.Runs().back()["taskLedgerDisposition"] = "already_running";
				m_store.Runs().back()["taskLedgerTerminal"] = true;
				EmitTaskLedgerTerminalHook(m_store.Runs().back());
				runsChanged = true;
				continue;
			}

			const auto nextRunAtMs = TryReadInt64Field((*job)["state"], "nextRunAtMs");
			if (request.mode == "due" &&
				(!nextRunAtMs.has_value() || nextRunAtMs.value() > nowMs)) {
				m_store.Runs().push_back(BuildManualLifecycleEntry(
					request.runId,
					request.jobId,
					normalizedMode,
					"finished",
					"skipped",
					"terminal",
					"not_due",
					"manual run skipped because cron job is not due",
					nowMs,
					request.queuedAtMs,
					std::nullopt,
					nowMs));
				m_store.Runs().back()["taskLedgerDisposition"] = "not_due";
				m_store.Runs().back()["taskLedgerTerminal"] = true;
				EmitTaskLedgerTerminalHook(m_store.Runs().back());
				runsChanged = true;
				continue;
			}

			m_store.Runs().push_back(BuildManualLifecycleEntry(
				request.runId,
				request.jobId,
				normalizedMode,
				"started",
				"running",
				"active",
				"started",
				"manual run dequeued and dispatched",
				nowMs,
				request.queuedAtMs,
				nowMs,
				std::nullopt));
			m_store.Runs().back()["taskLedgerDisposition"] = "started";
			m_store.Runs().back()["taskLedgerTerminal"] = false;
			EmitTaskLedgerCreateRunningHook(m_store.Runs().back());
			runsChanged = true;

			const std::size_t runsBeforeDispatch = m_store.Runs().size();

			(*job)["state"]["nextRunAtMs"] = nowMs;
			jobsChanged = true;
			SyncDueRunsLocked(nowMs, true);

			const CronJson* finishedRun = nullptr;
			for (std::size_t index = m_store.Runs().size(); index > runsBeforeDispatch; --index) {
				const CronJson& candidate = m_store.Runs()[index - 1];
				if (ReadStringOrEmpty(candidate, "jobId") != request.jobId) {
					continue;
				}
				if (ToLowerCopy(ReadStringOrEmpty(candidate, "action")) == "finished") {
					finishedRun = &candidate;
					break;
				}
			}

			if (finishedRun != nullptr) {
				const std::string mappedStatus = MapTerminalStatus(*finishedRun);
				const std::string mappedDisposition = MapTerminalDisposition(*finishedRun);
				CronJson terminal = BuildManualLifecycleEntry(
					request.runId,
					request.jobId,
					normalizedMode,
					"finished",
					mappedStatus,
					"terminal",
					mappedDisposition,
					"manual run completed after dispatch",
					nowMs,
					request.queuedAtMs,
					nowMs,
					nowMs);
				terminal["sourceRunId"] =
					finishedRun->contains("runId") ? (*finishedRun)["runId"] : CronJson(nullptr);
				terminal["sourceStatus"] =
					finishedRun->contains("status") ? (*finishedRun)["status"] : CronJson(nullptr);
				terminal["timedOut"] = finishedRun->value("timedOut", false);
				terminal["aborted"] =
					finishedRun->value("aborted", false) ||
					ToLowerCopy(ReadStringOrEmpty(*finishedRun, "errorCategory")) == "aborted";
				terminal["deliveryStatus"] =
					finishedRun->contains("deliveryStatus")
					? (*finishedRun)["deliveryStatus"]
					: CronJson("not-requested");
				terminal["deliveryMode"] =
					finishedRun->contains("deliveryMode")
					? (*finishedRun)["deliveryMode"]
					: CronJson(nullptr);
				terminal["deliveryTarget"] =
					finishedRun->contains("deliveryTarget")
					? (*finishedRun)["deliveryTarget"]
					: CronJson(nullptr);
				terminal["deliveryAttempted"] =
					finishedRun->contains("deliveryAttempted")
					? (*finishedRun)["deliveryAttempted"]
					: CronJson(false);
				terminal["deliveryHttpStatus"] =
					finishedRun->contains("deliveryHttpStatus")
					? (*finishedRun)["deliveryHttpStatus"]
					: CronJson(nullptr);
				terminal["deliveryError"] =
					finishedRun->contains("deliveryError")
					? (*finishedRun)["deliveryError"]
					: CronJson(nullptr);
				terminal["deliveryChannel"] =
					finishedRun->contains("deliveryChannel")
					? (*finishedRun)["deliveryChannel"]
					: CronJson(nullptr);
				terminal["deliveryAccountId"] =
					finishedRun->contains("deliveryAccountId")
					? (*finishedRun)["deliveryAccountId"]
					: CronJson(nullptr);
				terminal["failureDestinationStatus"] =
					finishedRun->contains("failureDestinationStatus")
					? (*finishedRun)["failureDestinationStatus"]
					: CronJson("not-requested");
				terminal["failureDestinationMode"] =
					finishedRun->contains("failureDestinationMode")
					? (*finishedRun)["failureDestinationMode"]
					: CronJson(nullptr);
				terminal["failureDestinationTarget"] =
					finishedRun->contains("failureDestinationTarget")
					? (*finishedRun)["failureDestinationTarget"]
					: CronJson(nullptr);
				terminal["failureDestinationAttempted"] =
					finishedRun->contains("failureDestinationAttempted")
					? (*finishedRun)["failureDestinationAttempted"]
					: CronJson(false);
				terminal["failureDestinationHttpStatus"] =
					finishedRun->contains("failureDestinationHttpStatus")
					? (*finishedRun)["failureDestinationHttpStatus"]
					: CronJson(nullptr);
				terminal["failureDestinationChannel"] =
					finishedRun->contains("failureDestinationChannel")
					? (*finishedRun)["failureDestinationChannel"]
					: CronJson(nullptr);
				terminal["failureDestinationAccountId"] =
					finishedRun->contains("failureDestinationAccountId")
					? (*finishedRun)["failureDestinationAccountId"]
					: CronJson(nullptr);
				terminal["failureDestinationError"] =
					finishedRun->contains("failureDestinationError")
					? (*finishedRun)["failureDestinationError"]
					: CronJson(nullptr);
				terminal["error"] =
					finishedRun->contains("error")
					? (*finishedRun)["error"]
					: CronJson(nullptr);
				terminal["errorCategory"] =
					finishedRun->contains("errorCategory")
					? (*finishedRun)["errorCategory"]
					: CronJson(nullptr);
				terminal["sessionId"] =
					finishedRun->contains("sessionId")
					? (*finishedRun)["sessionId"]
					: CronJson(nullptr);
				terminal["sessionKey"] =
					finishedRun->contains("sessionKey")
					? (*finishedRun)["sessionKey"]
					: CronJson(nullptr);
				terminal["model"] =
					finishedRun->contains("model")
					? (*finishedRun)["model"]
					: CronJson(nullptr);
				terminal["provider"] =
					finishedRun->contains("provider")
					? (*finishedRun)["provider"]
					: CronJson(nullptr);
				terminal["usage"] =
					finishedRun->contains("usage")
					? (*finishedRun)["usage"]
					: CronJson(nullptr);
				terminal["retryAttempt"] =
					finishedRun->contains("retryAttempt")
					? (*finishedRun)["retryAttempt"]
					: CronJson(0);
				terminal["retryScheduled"] =
					finishedRun->contains("retryScheduled")
					? (*finishedRun)["retryScheduled"]
					: CronJson(false);
				terminal["retryScheduledAtMs"] =
					finishedRun->contains("retryScheduledAtMs")
					? (*finishedRun)["retryScheduledAtMs"]
					: CronJson(nullptr);
				terminal["nextRunAtMs"] =
					finishedRun->contains("nextRunAtMs")
					? (*finishedRun)["nextRunAtMs"]
					: CronJson(nullptr);
				terminal["heartbeatBusyAttempts"] =
					finishedRun->contains("heartbeatBusyAttempts")
					? (*finishedRun)["heartbeatBusyAttempts"]
					: CronJson(0);
				terminal["heartbeatFallbackWakeRequested"] =
					finishedRun->contains("heartbeatFallbackWakeRequested")
					? (*finishedRun)["heartbeatFallbackWakeRequested"]
					: CronJson(false);
				terminal["heartbeatFallbackWakeRequestedAtMs"] =
					finishedRun->contains("heartbeatFallbackWakeRequestedAtMs")
					? (*finishedRun)["heartbeatFallbackWakeRequestedAtMs"]
					: CronJson(nullptr);
				terminal["failureAlertTriggered"] =
					finishedRun->contains("failureAlertTriggered")
					? (*finishedRun)["failureAlertTriggered"]
					: CronJson(false);
				terminal["failureAlertSuppressed"] =
					finishedRun->contains("failureAlertSuppressed")
					? (*finishedRun)["failureAlertSuppressed"]
					: CronJson(false);
				terminal["failureAlertSuppressedReason"] =
					finishedRun->contains("failureAlertSuppressedReason")
					? (*finishedRun)["failureAlertSuppressedReason"]
					: CronJson(nullptr);
				terminal["failureAlertMode"] =
					finishedRun->contains("failureAlertMode")
					? (*finishedRun)["failureAlertMode"]
					: CronJson(nullptr);
				terminal["failureAlertTarget"] =
					finishedRun->contains("failureAlertTarget")
					? (*finishedRun)["failureAlertTarget"]
					: CronJson(nullptr);
				terminal["failureAlertChannel"] =
					finishedRun->contains("failureAlertChannel")
					? (*finishedRun)["failureAlertChannel"]
					: CronJson(nullptr);
				terminal["failureAlertAccountId"] =
					finishedRun->contains("failureAlertAccountId")
					? (*finishedRun)["failureAlertAccountId"]
					: CronJson(nullptr);
				terminal["failureAlertAtMs"] =
					finishedRun->contains("failureAlertAtMs")
					? (*finishedRun)["failureAlertAtMs"]
					: CronJson(nullptr);
				terminal["taskLedgerDisposition"] = mappedDisposition;
				terminal["taskLedgerTerminal"] = true;
				m_store.Runs().push_back(terminal);
				EmitTaskLedgerTerminalHook(m_store.Runs().back());
			}
			else {
				m_store.Runs().push_back(BuildManualLifecycleEntry(
					request.runId,
					request.jobId,
					normalizedMode,
					"finished",
					"failed",
					"terminal",
					"missing_terminal_run",
					"manual run dispatch completed without observable terminal run record",
					nowMs,
					request.queuedAtMs,
					nowMs,
					nowMs));
				m_store.Runs().back()["taskLedgerDisposition"] = "missing_terminal_run";
				m_store.Runs().back()["taskLedgerTerminal"] = true;
				EmitTaskLedgerTerminalHook(m_store.Runs().back());
			}
			runsChanged = true;
		}

		if (jobsChanged) {
			m_store.SaveJobs();
		}
		if (runsChanged) {
			m_store.SaveRuns();
		}
	}

	void CronOpsService::RefreshSchedulesOnlyLocked(const std::int64_t nowMs) {
		CronRecomputeOptions recomputeOptions;
		recomputeOptions.preserveDueSlots = true;
		const bool changed = m_timer.RecomputeSchedules(
			m_store.Jobs(),
			nowMs,
			recomputeOptions);
		if (changed) {
			m_store.SaveJobs();
		}
		m_lastSyncAtMs = nowMs;
	}

	void CronOpsService::SyncDueRunsLocked(
		const std::int64_t nowMs,
		const bool forceRunDue) {
		CronRecomputeOptions executionRecomputeOptions;
		executionRecomputeOptions.preserveDueSlots = false;
		bool changed = m_timer.RecomputeSchedules(
			m_store.Jobs(),
			nowMs,
			executionRecomputeOptions);
		std::size_t executedTotal = 0;
		std::size_t loops = 0;

		while (loops < m_maxCatchupRunsPerSync) {
			const std::size_t runsBeforePump = m_store.Runs().size();
			const std::size_t executed =
				m_timer.PumpDueRuns(m_store.Jobs(), m_store.Runs(), nowMs, forceRunDue);
			if (executed == 0) {
				break;
			}

			if (!forceRunDue) {
				for (std::size_t index = runsBeforePump; index < m_store.Runs().size(); ++index) {
					const CronJson& runEntry = m_store.Runs()[index];
					if (ToLowerCopy(ReadStringOrEmpty(runEntry, "action")) != "finished") {
						continue;
					}
					EmitTaskLedgerCreateRunningHook(runEntry);
					EmitTaskLedgerTerminalHook(runEntry);
				}
			}

			executedTotal += executed;
			changed = true;
			m_timer.RecomputeSchedules(
				m_store.Jobs(),
				nowMs,
				executionRecomputeOptions);
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

	void CronOpsService::EmitTaskLedgerCreateRunningHook(const CronJson& runEntry) {
		if (!static_cast<bool>(m_taskLedgerHooks.createRunningTaskRun)) {
			return;
		}

		CronJson payload = BuildTaskLedgerHookPayload(runEntry);
		payload["status"] = "running";
		payload["taskLedgerStatus"] = "running";
		payload["taskLedgerPhase"] = "active";
		payload["taskLedgerTerminal"] = false;
		payload["phase"] = "active";
		payload["terminal"] = false;
		try {
			m_taskLedgerHooks.createRunningTaskRun(payload);
		}
		catch (...) {
		}
	}

	void CronOpsService::EmitTaskLedgerTerminalHook(const CronJson& runEntry) {
		const std::string mappedStatus = MapTerminalStatus(runEntry);
		const std::string mappedDisposition = MapTerminalDisposition(runEntry);
		const bool failed = IsFailureTaskLedgerStatus(mappedStatus);
		const TaskLedgerHook& hook = failed
			? m_taskLedgerHooks.failTaskRunByRunId
			: m_taskLedgerHooks.completeTaskRunByRunId;
		if (!static_cast<bool>(hook)) {
			return;
		}

		CronJson payload = BuildTaskLedgerHookPayload(runEntry);
		payload["status"] = mappedStatus;
		payload["taskLedgerStatus"] = mappedStatus;
		payload["disposition"] = mappedDisposition;
		payload["taskLedgerDisposition"] = mappedDisposition;
		payload["timedOut"] =
			runEntry.value("timedOut", false) ||
			ToLowerCopy(ReadStringOrEmpty(runEntry, "errorCategory")) == "timeout";
		payload["aborted"] =
			runEntry.value("aborted", false) ||
			ToLowerCopy(ReadStringOrEmpty(runEntry, "errorCategory")) == "aborted";
		payload["phase"] = "terminal";
		payload["terminal"] = true;
		try {
			hook(payload);
		}
		catch (...) {
		}
	}

	CronOpsService& GetCronOpsService() {
		static CronOpsService service;
		return service;
	}

} // namespace blazeclaw::cron
