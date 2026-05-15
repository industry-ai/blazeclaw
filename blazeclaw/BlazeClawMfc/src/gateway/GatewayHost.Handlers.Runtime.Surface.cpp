#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayHostRuntimeLocalHelpers.h"
#include "GatewayJsonUtils.h"
#include "Telemetry.h"
#include "ChatRunStageContext.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "RuntimeSequencingPolicy.h"
#include "RuntimeToolCallNormalizer.h"
#include "RuntimeTranscriptGuard.h"
#include "RecoveryPolicyEngine.h"
#include "SendPolicyResolver.h"
#include "ToolPolicyPipeline.h"
#include "TranscriptPolicyResolver.h"
#include "GatewayLifecycleEventEmitter.h"
#include "RunSummaryBuilder.h"
#include "BranchDecisionDiagnostics.h"
#include "ChatTranscriptStore.h"
#include "ChatAbortCoordinator.h"
#include "ChatHistoryPolicy.h"
#include "ChatRoutePolicy.h"
#include "ChatOrchestrationPolicy.h"
#include "ToolEventRecipientPolicy.h"
#include "ChatControlPlaneService.h"
#include "GatewayEventFanoutService.h"
#include "GatewayPersistencePaths.h"
#include "executors/EmailScheduleExecutor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace {
		using CronJson = nlohmann::json;

		std::int64_t UtcNowMs() {
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
				.count();
		}

		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string TrimCopy(const std::string& value) {
			std::size_t start = 0;
			std::size_t end = value.size();
			while (start < end &&
				std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			while (end > start &&
				std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		CronJson ParseParamsJson(const std::optional<std::string>& rawParams) {
			if (!rawParams.has_value() || TrimCopy(rawParams.value()).empty()) {
				return CronJson::object();
			}

			try {
				CronJson parsed = CronJson::parse(rawParams.value());
				if (parsed.is_object()) {
					return parsed;
				}
			}
			catch (...) {
			}

			throw std::invalid_argument("params must be a JSON object");
		}

		std::optional<std::int64_t> TryReadInt64Field(
			const CronJson& value,
			const char* key) {
			if (!value.is_object() || !value.contains(key) || value[key].is_null()) {
				return std::nullopt;
			}

			const CronJson& field = value[key];
			if (field.is_number_integer()) {
				return field.get<std::int64_t>();
			}
			if (field.is_number_unsigned()) {
				return static_cast<std::int64_t>(field.get<std::uint64_t>());
			}
			if (field.is_number_float()) {
				return static_cast<std::int64_t>(field.get<double>());
			}
			if (field.is_string()) {
				try {
					return std::stoll(TrimCopy(field.get<std::string>()));
				}
				catch (...) {
					return std::nullopt;
				}
			}

			return std::nullopt;
		}

		std::string ResolveCronId(const CronJson& params) {
			if (!params.is_object()) {
				return {};
			}

			if (params.contains("id") && params["id"].is_string()) {
				return TrimCopy(params["id"].get<std::string>());
			}
			if (params.contains("jobId") && params["jobId"].is_string()) {
				return TrimCopy(params["jobId"].get<std::string>());
			}

			return {};
		}

		class CronRuntimeService {
		public:
			CronJson Status(const CronJson& params) {
				(void)params;
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();
				RecomputeSchedulesLocked();
				const std::int64_t nextWakeAtMs = ComputeNextWakeAtMsLocked();
				return {
					{ "enabled", true },
					{ "storePath", m_jobsPath.string() },
					{ "jobs", m_jobs.size() },
					{ "nextWakeAtMs", nextWakeAtMs > 0 ? CronJson(nextWakeAtMs) : CronJson(nullptr) }
				};
			}

			CronJson List(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();
				RecomputeSchedulesLocked();

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
				filtered.reserve(m_jobs.size());
				for (const auto& job : m_jobs) {
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

			CronJson Add(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();

				const std::string name = TrimCopy(params.value("name", std::string()));
				if (name.empty()) {
					throw std::invalid_argument("`name` must be a non-empty string");
				}
				if (!params.contains("schedule") || !params["schedule"].is_object()) {
					throw std::invalid_argument("`schedule` must be an object");
				}
				if (!params.contains("payload") || !params["payload"].is_object()) {
					throw std::invalid_argument("`payload` must be an object");
				}

				const std::int64_t nowMs = UtcNowMs();
				const std::string generatedId =
					"cron-" + std::to_string(nowMs) + "-" + std::to_string(++m_idCounter);

				CronJson job = {
					{ "id", generatedId },
					{ "name", name },
					{ "description", params.value("description", std::string()) },
					{ "enabled", params.value("enabled", true) },
					{ "createdAtMs", nowMs },
					{ "updatedAtMs", nowMs },
					{ "schedule", params["schedule"] },
					{ "payload", params["payload"] },
					{ "wakeMode", ResolveWakeMode(params) },
					{ "sessionTarget", ResolveSessionTarget(params) },
					{ "deleteAfterRun", params.value("deleteAfterRun", false) },
					{ "state", CronJson::object() }
				};

				if (params.contains("delivery") && params["delivery"].is_object()) {
					job["delivery"] = params["delivery"];
				}
				if (params.contains("agentId") && params["agentId"].is_string()) {
					job["agentId"] = TrimCopy(params["agentId"].get<std::string>());
				}
				if (params.contains("sessionKey") && params["sessionKey"].is_string()) {
					job["sessionKey"] = TrimCopy(params["sessionKey"].get<std::string>());
				}

				const std::optional<std::int64_t> nextRunAtMs =
					ComputeNextRunAtMsLocked(job, nowMs);
				job["state"]["nextRunAtMs"] =
					nextRunAtMs.has_value() ? CronJson(nextRunAtMs.value()) : CronJson(nullptr);

				m_jobs.push_back(job);
				SaveJobsLocked();
				return job;
			}

			CronJson Update(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();

				const std::string id = ResolveCronId(params);
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

				const CronJson& patch = params["patch"];
				ApplyPatchFields(*job, patch);
				const std::int64_t nowMs = UtcNowMs();
				(*job)["updatedAtMs"] = nowMs;
				CronJson& state = EnsureStateObject(*job);
				const bool enabled = (*job).value("enabled", true);
				state["nextRunAtMs"] = enabled
					? CronJson(ComputeNextRunAtMsLocked(*job, nowMs).value_or(0))
					: CronJson(nullptr);
				if (state["nextRunAtMs"].is_number_integer() &&
					state["nextRunAtMs"].get<std::int64_t>() <= 0) {
					state["nextRunAtMs"] = nullptr;
				}

				SaveJobsLocked();
				return *job;
			}

			CronJson Remove(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();

				const std::string id = ResolveCronId(params);
				if (id.empty()) {
					throw std::invalid_argument("missing `id` or `jobId`");
				}

				const std::size_t before = m_jobs.size();
				m_jobs.erase(
					std::remove_if(
						m_jobs.begin(),
						m_jobs.end(),
						[id](const CronJson& job) {
							return job.value("id", std::string()) == id;
						}),
					m_jobs.end());

				const bool removed = m_jobs.size() != before;
				if (removed) {
					SaveJobsLocked();
				}

				return {
					{ "ok", true },
					{ "removed", removed }
				};
			}

			CronJson Run(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();
				const std::string id = ResolveCronId(params);
				if (id.empty()) {
					throw std::invalid_argument("missing `id` or `jobId`");
				}

				CronJson* job = FindJobByIdLocked(id);
				if (job == nullptr) {
					throw std::invalid_argument("unknown cron job id");
				}

				const std::string mode =
					ToLowerCopy(TrimCopy(params.value("mode", std::string("force"))));
				const std::int64_t nowMs = UtcNowMs();
				CronJson& state = EnsureStateObject(*job);

				const std::optional<std::int64_t> nextRunAtMs =
					ReadNextRunAtMsLocked(*job);
				if (mode == "due" &&
					(!nextRunAtMs.has_value() || nextRunAtMs.value() > nowMs)) {
					return {
						{ "runId", BuildRunId(nowMs) },
						{ "started", false },
						{ "reason", "not_due" },
						{ "cronId", id },
						{ "mode", "due" },
						{ "queuedAtMs", nowMs }
					};
				}

				state["runningAtMs"] = nowMs;
				state["lastRunAtMs"] = nowMs;
				state["lastStatus"] = "ok";
				state["lastRunStatus"] = "ok";
				state["lastDurationMs"] = 0;
				state["runningAtMs"] = nullptr;

				const std::string jobName = (*job).value("name", std::string());
				const bool deleteAfterRun = (*job).value("deleteAfterRun", false);
				std::optional<std::int64_t> nextAfterRun;
				if (deleteAfterRun) {
					m_jobs.erase(
						std::remove_if(
							m_jobs.begin(),
							m_jobs.end(),
							[id](const CronJson& candidate) {
								return candidate.value("id", std::string()) == id;
							}),
						m_jobs.end());
				}
				else {
					nextAfterRun = ComputeNextRunAtMsLocked(*job, nowMs);
					state["nextRunAtMs"] =
						nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr);
					(*job)["updatedAtMs"] = nowMs;
				}

				const CronJson runEntry = {
					{ "ts", nowMs },
					{ "jobId", id },
					{ "action", "finished" },
					{ "status", "ok" },
					{ "deliveryStatus", "not-requested" },
					{ "durationMs", 0 },
					{ "runAtMs", nowMs },
					{ "nextRunAtMs", deleteAfterRun ? CronJson(nullptr) : (nextAfterRun.has_value() ? CronJson(nextAfterRun.value()) : CronJson(nullptr)) },
					{ "jobName", jobName }
				};
				m_runs.push_back(runEntry);
				SaveJobsLocked();
				SaveRunsLocked();

				return {
					{ "runId", BuildRunId(nowMs) },
					{ "started", true },
					{ "cronId", id },
					{ "mode", mode == "due" ? "due" : "force" },
					{ "queuedAtMs", nowMs }
				};
			}

			CronJson Runs(const CronJson& params) {
				std::lock_guard<std::mutex> lock(m_mutex);
				EnsureLoadedLocked();

				const std::size_t requestedLimit =
					ClampLimit(params.value("limit", 20), 1, 200, 20);
				const std::size_t requestedOffset =
					ClampLimit(params.value("offset", 0), 0, 1'000'000, 0);
				const std::string scope =
					ToLowerCopy(TrimCopy(params.value("scope", std::string("all"))));
				const std::string requestedId = ResolveCronId(params);
				const std::string statusFilter =
					ToLowerCopy(TrimCopy(params.value("status", std::string("all"))));
				const std::string query =
					ToLowerCopy(TrimCopy(params.value("query", std::string())));
				const std::string sortDir =
					ToLowerCopy(TrimCopy(params.value("sortDir", std::string("desc"))));

				std::vector<CronJson> filtered;
				filtered.reserve(m_runs.size());
				for (const auto& entry : m_runs) {
					const std::string jobId = entry.value("jobId", std::string());
					const std::string status =
						ToLowerCopy(entry.value("status", std::string()));

					if (scope == "job" && !requestedId.empty() && jobId != requestedId) {
						continue;
					}
					if (statusFilter != "" && statusFilter != "all" && status != statusFilter) {
						continue;
					}
					if (!query.empty()) {
						const std::string haystack = ToLowerCopy(
							jobId + " " +
							entry.value("status", std::string()) + " " +
							entry.value("jobName", std::string()) + " " +
							entry.value("error", std::string()));
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

			CronJson Wake(const CronJson& params) {
				const std::string mode =
					ToLowerCopy(TrimCopy(params.value("mode", std::string("now"))));
				if (mode != "now" && mode != "next-heartbeat") {
					throw std::invalid_argument("`mode` must be `now` or `next-heartbeat`");
				}

				return {
					{ "ok", true },
					{ "mode", mode },
					{ "text", params.value("text", std::string()) },
					{ "requestedAtMs", UtcNowMs() }
				};
			}

		private:
			std::mutex m_mutex;
			bool m_loaded = false;
			CronJson m_jobs = CronJson::array();
			CronJson m_runs = CronJson::array();
			std::uint64_t m_idCounter = 0;
			const std::filesystem::path m_jobsPath =
				ResolveGatewayStateFilePath("cron.jobs.json");
			const std::filesystem::path m_runsPath =
				ResolveGatewayStateFilePath("cron.runs.json");

			static std::string BuildRunId(const std::int64_t nowMs) {
				return std::string("cron-run-") + std::to_string(nowMs);
			}

			static std::size_t ClampLimit(
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

			static CronJson& EnsureStateObject(CronJson& job) {
				if (!job.contains("state") || !job["state"].is_object()) {
					job["state"] = CronJson::object();
				}
				return job["state"];
			}

			std::string ResolveWakeMode(const CronJson& params) const {
				const std::string raw =
					ToLowerCopy(TrimCopy(params.value("wakeMode", std::string("now"))));
				if (raw == "now" || raw == "next-heartbeat") {
					return raw;
				}
				return "now";
			}

			std::string ResolveSessionTarget(const CronJson& params) const {
				const std::string raw = TrimCopy(
					params.value("sessionTarget", std::string()));
				if (!raw.empty()) {
					return raw;
				}

				if (params.contains("payload") && params["payload"].is_object()) {
					const std::string payloadKind =
						ToLowerCopy(TrimCopy(params["payload"].value("kind", std::string())));
					if (payloadKind == "agentturn") {
						return "isolated";
					}
				}

				return "main";
			}

			CronJson* FindJobByIdLocked(const std::string& id) {
				for (auto& job : m_jobs) {
					if (job.value("id", std::string()) == id) {
						return &job;
					}
				}
				return nullptr;
			}

			void ApplyPatchFields(CronJson& job, const CronJson& patch) {
				if (patch.contains("name") && patch["name"].is_string()) {
					const std::string name = TrimCopy(patch["name"].get<std::string>());
					if (!name.empty()) {
						job["name"] = name;
					}
				}
				if (patch.contains("description") && patch["description"].is_string()) {
					job["description"] = patch["description"].get<std::string>();
				}
				if (patch.contains("enabled") && patch["enabled"].is_boolean()) {
					job["enabled"] = patch["enabled"].get<bool>();
				}
				if (patch.contains("schedule") && patch["schedule"].is_object()) {
					job["schedule"] = patch["schedule"];
				}
				if (patch.contains("payload") && patch["payload"].is_object()) {
					job["payload"] = patch["payload"];
				}
				if (patch.contains("delivery") && patch["delivery"].is_object()) {
					job["delivery"] = patch["delivery"];
				}
				if (patch.contains("sessionTarget") && patch["sessionTarget"].is_string()) {
					job["sessionTarget"] = TrimCopy(patch["sessionTarget"].get<std::string>());
				}
				if (patch.contains("wakeMode") && patch["wakeMode"].is_string()) {
					const std::string wakeMode =
						ToLowerCopy(TrimCopy(patch["wakeMode"].get<std::string>()));
					if (wakeMode == "now" || wakeMode == "next-heartbeat") {
						job["wakeMode"] = wakeMode;
					}
				}
				if (patch.contains("deleteAfterRun") && patch["deleteAfterRun"].is_boolean()) {
					job["deleteAfterRun"] = patch["deleteAfterRun"].get<bool>();
				}
				if (patch.contains("agentId") && patch["agentId"].is_string()) {
					job["agentId"] = TrimCopy(patch["agentId"].get<std::string>());
				}
				if (patch.contains("sessionKey") && patch["sessionKey"].is_string()) {
					job["sessionKey"] = TrimCopy(patch["sessionKey"].get<std::string>());
				}
			}

			std::optional<std::int64_t> ReadNextRunAtMsLocked(const CronJson& job) {
				if (!job.contains("state") || !job["state"].is_object()) {
					return std::nullopt;
				}
				return TryReadInt64Field(job["state"], "nextRunAtMs");
			}

			std::optional<std::int64_t> ComputeNextRunAtMsLocked(
				const CronJson& job,
				const std::int64_t nowMs) {
				if (!job.value("enabled", true)) {
					return std::nullopt;
				}
				if (!job.contains("schedule") || !job["schedule"].is_object()) {
					return std::nullopt;
				}

				const CronJson& schedule = job["schedule"];
				const std::string kind =
					ToLowerCopy(TrimCopy(schedule.value("kind", std::string())));

				if (kind == "every") {
					const std::int64_t everyMs =
						(std::max)(static_cast<std::int64_t>(1000),
							TryReadInt64Field(schedule, "everyMs").value_or(60'000));
					const auto lastRunAtMs =
						job.contains("state") && job["state"].is_object()
						? TryReadInt64Field(job["state"], "lastRunAtMs")
						: std::nullopt;
					if (lastRunAtMs.has_value() && lastRunAtMs.value() + everyMs > nowMs) {
						return lastRunAtMs.value() + everyMs;
					}
					return nowMs + everyMs;
				}

				if (kind == "at") {
					const auto atMs = TryReadInt64Field(schedule, "atMs");
					if (atMs.has_value()) {
						const auto lastStatus =
							job.contains("state") && job["state"].is_object()
							? ToLowerCopy(job["state"].value("lastStatus", std::string()))
							: std::string();
						if (lastStatus == "ok") {
							return std::nullopt;
						}
						return atMs.value();
					}
					return nowMs + 5 * 60 * 1000;
				}

				if (kind == "cron") {
					return nowMs + 60 * 1000;
				}

				return std::nullopt;
			}

			std::int64_t ComputeNextWakeAtMsLocked() const {
				std::int64_t nextWakeAtMs = 0;
				for (const auto& job : m_jobs) {
					if (!job.value("enabled", true)) {
						continue;
					}
					if (!job.contains("state") || !job["state"].is_object()) {
						continue;
					}
					const auto maybeNext = TryReadInt64Field(job["state"], "nextRunAtMs");
					if (!maybeNext.has_value() || maybeNext.value() <= 0) {
						continue;
					}
					if (nextWakeAtMs <= 0 || maybeNext.value() < nextWakeAtMs) {
						nextWakeAtMs = maybeNext.value();
					}
				}
				return nextWakeAtMs;
			}

			void RecomputeSchedulesLocked() {
				const std::int64_t nowMs = UtcNowMs();
				bool changed = false;
				for (auto& job : m_jobs) {
					CronJson& state = EnsureStateObject(job);
					const std::optional<std::int64_t> nextRunAtMs =
						ComputeNextRunAtMsLocked(job, nowMs);
					const CronJson nextJson =
						nextRunAtMs.has_value() ? CronJson(nextRunAtMs.value()) : CronJson(nullptr);
					if (!state.contains("nextRunAtMs") || state["nextRunAtMs"] != nextJson) {
						state["nextRunAtMs"] = nextJson;
						changed = true;
					}
				}

				if (changed) {
					SaveJobsLocked();
				}
			}

			void EnsureLoadedLocked() {
				if (m_loaded) {
					return;
				}

				std::error_code ec;
				std::filesystem::create_directories(m_jobsPath.parent_path(), ec);

				m_jobs = LoadArrayFile(m_jobsPath);
				m_runs = LoadArrayFile(m_runsPath);
				m_idCounter = static_cast<std::uint64_t>(m_jobs.size() + m_runs.size());
				m_loaded = true;
			}

			CronJson LoadArrayFile(const std::filesystem::path& path) {
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
				}
				catch (...) {
				}

				return CronJson::array();
			}

			void SaveJobsLocked() {
				SaveArrayFile(m_jobsPath, m_jobs);
			}

			void SaveRunsLocked() {
				SaveArrayFile(m_runsPath, m_runs);
			}

			void SaveArrayFile(
				const std::filesystem::path& path,
				const CronJson& values) {
				std::ofstream stream(path, std::ios::binary | std::ios::trunc);
				if (!stream.is_open()) {
					return;
				}
				stream << values.dump(2);
			}
		};

		CronRuntimeService& GetCronRuntimeService() {
			static CronRuntimeService runtimeService;
			return runtimeService;
		}

		protocol::ResponseFrame CronInvalidParams(
			const protocol::RequestFrame& request,
			const std::string& message) {
			return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = "invalid_params",
					.message = message,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});
		}

		template <typename TFunc>
		protocol::ResponseFrame HandleCronRequest(
			const protocol::RequestFrame& request,
			TFunc&& func) {
			try {
				const CronJson params = ParseParamsJson(request.paramsJson);
				CronJson payload = func(GetCronRuntimeService(), params);
				return protocol::OkResponse(request, payload.dump());
			}
			catch (const std::invalid_argument& ex) {
				return CronInvalidParams(request, ex.what());
			}
			catch (...) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "internal_error",
						.message = "cron operation failed",
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					});
			}
		}
	} // namespace

	namespace handlers::runtime {

		void RuntimeSurfaceHandlers::RegisterAll(GatewayHost& host) {
			using namespace blazeclaw::gateway::runtime_local;
			// P1-P5: Scheduler / cron contract handlers backed by persistent runtime storage.
			host.m_dispatcher.Register("cron.list", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.List(params);
					});
				});
			host.m_dispatcher.Register("cron.status", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Status(params);
					});
				});
			host.m_dispatcher.Register("cron.add", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Add(params);
					});
				});
			host.m_dispatcher.Register("cron.update", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Update(params);
					});
				});
			host.m_dispatcher.Register("cron.remove", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Remove(params);
					});
				});
			host.m_dispatcher.Register("cron.run", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Run(params);
					});
				});
			host.m_dispatcher.Register("cron.runs", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Runs(params);
					});
				});
			host.m_dispatcher.Register("wake", [](const protocol::RequestFrame& request) {
				return HandleCronRequest(
					request,
					[](CronRuntimeService& service, const CronJson& params) {
						return service.Wake(params);
					});
				});
			host.m_dispatcher.Register("wizard.start", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"started\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.next", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"next\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.cancel", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cancelled\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.status", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"status\":\"idle\",\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("talk.config", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"config\":{},\"updated\":true}");
				});
			host.m_dispatcher.Register("talk.speak", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"spoken\":true,\"text\":\"Hello!\"}");
				});
			host.m_dispatcher.Register("talk.mode", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"mode\":\"default\",\"updated\":true}");
				});
			host.m_dispatcher.Register("voicewake.get", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"enabled\":false,\"model\":\"default\"}");
				});
			struct RuntimeTtsState {
				bool enabled = false;
				std::string provider = "default";
				std::string model = "default";
				std::uint64_t convertSequence = 1;
				std::unordered_set<std::string> allowedProviders{ "default", "azure" };
			};
			struct RuntimeSecretsState {
				std::uint64_t reloadCount = 0;
				std::uint64_t reloadWarningCount = 0;
				std::unordered_set<std::string> knownTargetIds{ "runtime", "workspace", "session" };
			};
			auto runtimeTtsState = std::make_shared<RuntimeTtsState>();
			auto runtimeSecretsState = std::make_shared<RuntimeSecretsState>();

			host.m_dispatcher.Register("voicewake.set", [&host](const protocol::RequestFrame& request) {
				EmitTelemetryEvent(
					"gateway.event.voicewake.changed",
					std::string("{\"event\":\"voicewake.changed\",\"running\":") +
					(host.IsRunning() ? "true" : "false") + "}");
				return protocol::OkResponse(request, "{\"enabled\":true,\"model\":\"default\",\"updated\":true}");
				});
			host.m_dispatcher.Register("tts.status", [runtimeTtsState](const protocol::RequestFrame& request) {
				const std::string providersJson = "[\"default\",\"azure\"]";
				return protocol::OkResponse(
					request,
					"{\"enabled\":" + std::string(runtimeTtsState->enabled ? "true" : "false") +
					",\"provider\":\"" + EscapeJsonLocal(runtimeTtsState->provider) +
					"\",\"auto\":false,\"fallbackProvider\":\"default\",\"providers\":" + providersJson + "}");
				});
			host.m_dispatcher.Register("tts.enable", [runtimeTtsState](const protocol::RequestFrame& request) {
				runtimeTtsState->enabled = true;
				return protocol::OkResponse(request, "{\"enabled\":true,\"updated\":true}");
				});
			host.m_dispatcher.Register("tts.disable", [runtimeTtsState](const protocol::RequestFrame& request) {
				runtimeTtsState->enabled = false;
				return protocol::OkResponse(request, "{\"enabled\":false,\"updated\":true}");
				});
			host.m_dispatcher.Register("tts.providers", [runtimeTtsState](const protocol::RequestFrame& request) {
				const std::string providersJson =
					"[{\"id\":\"default\",\"name\":\"Default\",\"configured\":true,\"models\":[\"default\"],\"voices\":[\"alloy\"]},"
					"{\"id\":\"azure\",\"name\":\"Azure\",\"configured\":true,\"models\":[\"neural\"],\"voices\":[\"aria\"]}]";
				return protocol::OkResponse(
					request,
					"{\"providers\":" + providersJson + ",\"active\":\"" + EscapeJsonLocal(runtimeTtsState->provider) + "\"}");
				});
			host.m_dispatcher.Register("tts.convert", [runtimeTtsState](const protocol::RequestFrame& request) {
				const std::string text = ExtractStringParam(request.paramsJson, "text");
				if (text.empty()) {
					return protocol::ErrorResponse(request, "invalid_request", "tts.convert requires text");
				}
				const std::string providerOverride = ExtractStringParam(request.paramsJson, "provider");
				const std::string provider = providerOverride.empty() ? runtimeTtsState->provider : providerOverride;
				if (runtimeTtsState->allowedProviders.find(provider) == runtimeTtsState->allowedProviders.end()) {
					return protocol::ErrorResponse(request, "invalid_request", "Invalid provider. Use a registered TTS provider id.");
				}
				const std::string audioPath =
					"artifacts/tts/tts-" + std::to_string(runtimeTtsState->convertSequence++) + ".wav";
				return protocol::OkResponse(
					request,
					"{\"audioPath\":\"" + EscapeJsonLocal(audioPath) +
					"\",\"provider\":\"" + EscapeJsonLocal(provider) +
					"\",\"outputFormat\":\"wav\",\"voiceCompatible\":true,\"model\":\"" + EscapeJsonLocal(runtimeTtsState->model) + "\"}");
				});

			host.m_dispatcher.Register("secrets.reload", [runtimeSecretsState](const protocol::RequestFrame& request) {
				runtimeSecretsState->reloadCount += 1;
				runtimeSecretsState->reloadWarningCount = runtimeSecretsState->reloadCount % 2;
				return protocol::OkResponse(
					request,
					"{\"ok\":true,\"warningCount\":" + std::to_string(runtimeSecretsState->reloadWarningCount) +
					",\"reloadCount\":" + std::to_string(runtimeSecretsState->reloadCount) + "}");
				});
			host.m_dispatcher.Register("secrets.resolve", [runtimeSecretsState](const protocol::RequestFrame& request) {
				const std::string commandName = ExtractStringParam(request.paramsJson, "commandName");
				if (commandName.empty()) {
					return protocol::ErrorResponse(request, "invalid_request", "invalid secrets.resolve params: commandName");
				}
				std::string targetIdsJson = "[]";
				if (request.paramsJson.has_value()) {
					std::string rawTargetIds;
					if (json::FindRawField(request.paramsJson.value(), "targetIds", rawTargetIds)) {
						const std::string trimmed = json::Trim(rawTargetIds);
						if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
							targetIdsJson = trimmed;
						}
					}
				}
				for (const auto& knownTargetId : runtimeSecretsState->knownTargetIds) {
					if (targetIdsJson.find(knownTargetId) != std::string::npos) {
						continue;
					}
				}
				if (!targetIdsJson.empty() && targetIdsJson != "[]") {
					const bool hasKnownTarget =
						targetIdsJson.find("runtime") != std::string::npos ||
						targetIdsJson.find("workspace") != std::string::npos ||
						targetIdsJson.find("session") != std::string::npos;
					if (!hasKnownTarget) {
						return protocol::ErrorResponse(request, "invalid_request", "invalid secrets.resolve params: unknown target id");
					}
				}
				const std::string assignmentsJson =
					"[{\"path\":\"runtime.env.API_KEY\",\"pathSegments\":[\"runtime\",\"env\",\"API_KEY\"],\"value\":\"***\"}]";
				const std::string diagnosticsJson =
					"[\"resolved secrets for command \"" + EscapeJsonLocal(commandName) + "\"\"]";
				return protocol::OkResponse(
					request,
					"{\"ok\":true,\"commandName\":\"" + EscapeJsonLocal(commandName) +
					"\",\"targetIds\":" + targetIdsJson +
					",\"assignments\":" + assignmentsJson +
					",\"diagnostics\":" + diagnosticsJson +
					",\"inactiveRefPaths\":[]}");
				});
			host.m_dispatcher.Register(
				"gateway.runtime.plugins.capabilities",
				[&host](const protocol::RequestFrame& request) {
					const auto contracts =
						host.m_pluginRuntimeState.ListCapabilityContracts();

					return protocol::OkResponse(request, "{\"capabilities\":" +
						SerializePluginRuntimeCapabilitiesJsonLocal(contracts) +
						",\"count\":" +
						std::to_string(contracts.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.state",
				[&host](const protocol::RequestFrame& request) {
					const auto snapshot = host.m_pluginRuntimeState.Snapshot();
					const auto importedPluginIds =
						host.m_pluginRuntimeState.ListImportedRuntimePluginIds();

					std::string importedJson = "[";
					for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
						if (index > 0) {
							importedJson += ",";
						}

						importedJson +=
							"\"" +
							EscapeJsonLocal(importedPluginIds[index]) +
							"\"";
					}
					importedJson += "]";

					const std::size_t activeRegistryCount =
						snapshot.activeRegistry == nullptr
						? 0
						: snapshot.activeRegistry->size();

					return protocol::OkResponse(request, "{\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetChannelVersion()) +
						",\"activeRegistryCount\":" +
						std::to_string(activeRegistryCount) +
						",\"httpRoutePinned\":" +
						std::string(snapshot.httpRoute.pinned ? "true" : "false") +
						",\"channelPinned\":" +
						std::string(snapshot.channel.pinned ? "true" : "false") +
						",\"cacheKey\":\"" +
						EscapeJsonLocal(snapshot.cacheKey) +
						"\",\"workspaceDir\":\"" +
						EscapeJsonLocal(snapshot.workspaceDir) +
						"\",\"runtimeSubagentMode\":\"" +
						SerializePluginRuntimeSubagentModeLocal(
							snapshot.runtimeSubagentMode) +
						"\",\"importedPluginIds\":" +
						importedJson +
						",\"importedCount\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions",
				[&host](const protocol::RequestFrame& request) {
					const auto requestedLimit =
						ExtractSizeParam(request.paramsJson, "limit").value_or(20);
					const std::size_t limit =
						(std::max)(
							std::size_t{ 1 },
							(std::min)(requestedLimit, std::size_t{ 128 }));

					const auto transitions =
						host.m_pluginRuntimeState.GetTransitionHistory();
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							limit) +
						",\"count\":" +
						std::to_string((std::min)(transitions.size(), limit)) +
						",\"total\":" +
						std::to_string(transitions.size()) +
						",\"retention\":{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}" +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.policy.get",
				[&host](const protocol::RequestFrame& request) {
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.policy.set",
				[&host](const protocol::RequestFrame& request) {
					const auto historyLimit =
						ExtractSizeParam(request.paramsJson, "historyLimit").value_or(128);
					const auto exportEnabled =
						ExtractBoolParam(request.paramsJson, "exportEnabled").value_or(false);

					host.m_pluginRuntimeState.SetTransitionPolicySettings(
						PluginRuntimeStateService::TransitionPolicySettings{
							.historyLimit = historyLimit,
							.exportEnabled = exportEnabled,
						});

					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();
					return protocol::OkResponse(request, "{\"updated\":true,\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.export",
				[&host](const protocol::RequestFrame& request) {
					const auto transitions =
						host.m_pluginRuntimeState.ExportTransitionHistory();
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"enabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						",\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							transitions.size()) +
						",\"count\":" +
						std::to_string(transitions.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.imported.list",
				[&host](const protocol::RequestFrame& request) {
					const auto importedPluginIds =
						host.m_pluginRuntimeState.ListImportedRuntimePluginIds();
					std::string importedJson = "[";
					for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
						if (index > 0) {
							importedJson += ",";
						}

						importedJson +=
							"\"" +
							EscapeJsonLocal(importedPluginIds[index]) +
							"\"";
					}
					importedJson += "]";

					return protocol::OkResponse(request, "{\"plugins\":" +
						importedJson +
						",\"count\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.lifecycle.reset",
				[&host](const protocol::RequestFrame& request) {
					bool forTest = false;
					if (request.paramsJson.has_value()) {
						json::FindBoolField(
							request.paramsJson.value(),
							"forTest",
							forTest);
					}

					if (!forTest) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "Set forTest=true to reset plugin runtime lifecycle state.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					host.m_pluginRuntimeState.ResetForTest();
					const auto snapshot = host.m_pluginRuntimeState.Snapshot();

					return protocol::OkResponse(request, "{\"reset\":true,\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetChannelVersion()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.reportStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"governanceReportingEnabled\":" +
						std::string(state.governanceReportingEnabled ? "true" : "false") +
						",\"remediationTelemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"remediationAuditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\",\"autoRemediationTenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"autoRemediationTokenRotations\":" +
						std::to_string(state.autoRemediationTokenRotations) +
						",\"remediationSloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"remediationSloMaxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"remediationSloMaxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"lastComplianceAttestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"complianceAttestationEnabled\":" +
						std::string(!state.lastComplianceAttestationPath.empty() ? "true" : "false") +
						",\"enterpriseSlaPolicyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"crossTenantAttestationAggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"crossTenantAttestationAggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"crossTenantAttestationAggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"lastCrossTenantAttestationAggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\"" +
						",\"governanceReportsGenerated\":" +
						std::to_string(state.governanceReportsGenerated) +
						",\"lastGovernanceReportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"lastDriftReason\":\"" +
						EscapeJsonLocal(state.lastDriftReason) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.attestationStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"maxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"maxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"telemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"auditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.aggregationStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"policyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"aggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.remediationPlan",
				[&host](const protocol::RequestFrame& request) {
					(void)request;
					const auto& state = host.m_skillsCatalogState;
					std::string severity = "none";
					std::string recommendedAction = "monitor";
					if (state.driftDetectedCount > 0) {
						severity = "high";
						recommendedAction =
							"review drift report; enforce strict policy and investigate runtime divergence";
					}
					else if (state.policyBlockedCount > 0) {
						severity = "medium";
						recommendedAction =
							"review package allowlist policy and blocked package changes";
					}

					const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
					const std::uint64_t ttlMinutes =
						state.autoRemediationTokenMaxAgeMinutes > 0
						? static_cast<std::uint64_t>(state.autoRemediationTokenMaxAgeMinutes)
						: std::uint64_t{ 60 };
					const std::uint64_t expiresAtEpochMs =
						nowEpochMs + (ttlMinutes * std::uint64_t{ 60000 });

					std::string issuedApprovalToken;
					if (state.autoRemediationRequiresApproval &&
						state.autoRemediationEnabled) {
						issuedApprovalToken =
							"remediation-approval-" + std::to_string(nowEpochMs) +
							"-" + std::to_string(state.driftDetectedCount + state.policyBlockedCount + 1);

						const std::string payload =
							"{\"tenantId\":\"" +
							EscapeJsonLocal(state.autoRemediationTenantId) +
							"\",\"recommendedAction\":\"" +
							EscapeJsonLocal(recommendedAction) +
							"\",\"reportPath\":\"" +
							EscapeJsonLocal(state.lastGovernanceReportPath) +
							"\"}";

						const ApprovalSessionRecord session{
							.token = issuedApprovalToken,
							.type = "governance.remediation",
							.payloadJson = payload,
							.createdAtEpochMs = nowEpochMs,
							.expiresAtEpochMs = expiresAtEpochMs,
						};

						if (!host.m_approvalStore.SaveSession(session)) {
							issuedApprovalToken.clear();
						}

						host.m_approvalStore.PruneExpired(nowEpochMs);
					}

					return protocol::OkResponse(request, "{\"severity\":\"" + EscapeJsonLocal(severity) +
						"\",\"recommendedAction\":\"" +
						EscapeJsonLocal(recommendedAction) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"autoRemediationEnabled\":" +
						std::string(state.autoRemediationEnabled ? "true" : "false") +
						",\"autoRemediationRequiresApproval\":" +
						std::string(state.autoRemediationRequiresApproval ? "true" : "false") +
						",\"approvalToken\":\"" +
						EscapeJsonLocal(issuedApprovalToken) +
						"\",\"approvalTokenExpiresAtEpochMs\":" +
						std::to_string(expiresAtEpochMs) +
						",\"tokenMaxAgeMinutes\":" +
						std::to_string(ttlMinutes) +
						",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.executeRemediation",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					if (!state.autoRemediationEnabled) {
						return protocol::OkResponse(request, "{\"executed\":false,\"status\":\"disabled\",\"approvalAccepted\":false}");
					}

					bool approvalAccepted = false;
					std::string approvalToken;
					if (request.paramsJson.has_value()) {
						json::FindStringField(
							request.paramsJson.value(),
							"approvalToken",
							approvalToken);
					}
					if (state.autoRemediationRequiresApproval) {
						bool approved = false;
						if (request.paramsJson.has_value()) {
							json::FindBoolField(request.paramsJson.value(), "approved", approved);
						}
						approvalAccepted = approved;
						const bool tokenAccepted = !approvalToken.empty();
						if (!approvalAccepted || !tokenAccepted) {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_required",
												.message = "Auto-remediation execution requires explicit approval and token.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
						ApprovalSessionRecord approvalSession;
						if (!host.m_approvalStore.IsTokenValid(
							approvalToken,
							nowEpochMs,
							&approvalSession)) {
							const auto existing = host.m_approvalStore.LoadSession(approvalToken);
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = existing.has_value()
													? "approval_token_expired"
													: "approval_token_invalid",
												.message = existing.has_value()
													? "Approval token expired. Request a new remediation plan token."
													: "Approval token not found.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						if (approvalSession.type != "governance.remediation") {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_token_orphaned",
												.message = "Approval token type mismatch for remediation execution.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						std::string tokenTenantId;
						json::FindStringField(
							approvalSession.payloadJson,
							"tenantId",
							tokenTenantId);
						if (!tokenTenantId.empty() &&
							tokenTenantId != state.autoRemediationTenantId) {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_token_orphaned",
												.message = "Approval token tenant mismatch for remediation execution.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}
					}

					std::string action = "monitor";
					if (state.driftDetectedCount > 0) {
						action = "enable_strict_policy";
					}
					else if (state.policyBlockedCount > 0) {
						action = "refresh_allowlist_review";
					}

					if (state.autoRemediationRequiresApproval && !approvalToken.empty()) {
						host.m_approvalStore.RemoveToken(approvalToken);
					}

					return protocol::OkResponse(request, "{\"executed\":true,\"status\":\"applied\",\"approvalAccepted\":" +
						std::string(approvalAccepted ? "true" : "false") +
						",\"tenantId\":\"" + EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"playbookPath\":\"" +
						EscapeJsonLocal(state.lastAutoRemediationPlaybookPath) +
						"\",\"tokenMaxAgeMinutes\":" +
						std::to_string(state.autoRemediationTokenMaxAgeMinutes) +
						",\"action\":\"" + EscapeJsonLocal(action) +
						"\",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.embeddings.generate",
				[&host](const protocol::RequestFrame& request) {
					const std::string text =
						ExtractStringParam(request.paramsJson, "text");
					const std::optional<bool> normalize =
						ExtractBoolParam(request.paramsJson, "normalize");
					const std::string model =
						ExtractStringParam(request.paramsJson, "model");
					const std::string traceId =
						request.id.empty() ? "gateway.embeddings.generate" : request.id;

					if (text.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`text` must be a non-empty string.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (!host.m_embeddingsGenerateCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "runtime_unavailable",
										.message = "Embeddings runtime callback is unavailable.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto result = host.m_embeddingsGenerateCallback(
						GatewayHost::EmbeddingsGenerateRequest{
							.text = text,
							.normalize = normalize,
							.model = model,
							.traceId = traceId,
						});

					if (!result.ok) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = result.errorCode.empty()
											? "embedding_failed"
											: result.errorCode,
										.message = result.errorMessage.empty()
											? "Embedding generation failed."
											: result.errorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, "{\"vector\":" + SerializeFloatArrayLocal(result.vector) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.embeddings.batchGenerate",
				[&host](const protocol::RequestFrame& request) {
					std::string rawTexts;
					std::vector<std::string> texts;
					if (request.paramsJson.has_value() &&
						json::FindRawField(request.paramsJson.value(), "texts", rawTexts)) {
						texts = ParseJsonStringArrayLocal(rawTexts);
					}

					const std::optional<bool> normalize =
						ExtractBoolParam(request.paramsJson, "normalize");
					const std::string model =
						ExtractStringParam(request.paramsJson, "model");
					const std::string traceId =
						request.id.empty() ? "gateway.embeddings.batchGenerate" : request.id;

					if (texts.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`texts` must be a non-empty string array.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (texts.size() > 64) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`texts` exceeds maximum batch size of 64.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (!host.m_embeddingsBatchCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "runtime_unavailable",
										.message = "Embeddings runtime callback is unavailable.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto result = host.m_embeddingsBatchCallback(
						GatewayHost::EmbeddingsBatchRequest{
							.texts = texts,
							.normalize = normalize,
							.model = model,
							.traceId = traceId,
						});

					if (!result.ok) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = result.errorCode.empty()
											? "embedding_failed"
											: result.errorCode,
										.message = result.errorMessage.empty()
											? "Embedding batch generation failed."
											: result.errorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, "{\"vectors\":" + SerializeFloatMatrixLocal(result.vectors) +
						",\"count\":" + std::to_string(result.vectors.size()) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.taskDeltas.get",
				[&host](const protocol::RequestFrame& request) {
					const std::string runId =
						ExtractStringParam(request.paramsJson, "runId");
					if (runId.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "missing_run_id",
										.message = "runId is required.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto storedDeltas = host.m_taskDeltaRepository.Get(runId);
					if (!storedDeltas.has_value()) {
						return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
							"\",\"taskDeltas\":[],\"count\":0}");
					}

					auto orderedTaskDeltas = storedDeltas.value();
					std::sort(
						orderedTaskDeltas.begin(),
						orderedTaskDeltas.end(),
						[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& left,
							const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& right) {
								return left.index < right.index;
						});

					orderedTaskDeltas = TaskDeltaLegacyAdapter::AdaptRun(
						runId,
						orderedTaskDeltas.empty() ? std::string("main") : orderedTaskDeltas.front().sessionId,
						orderedTaskDeltas);

					std::string schemaErrorCode;
					std::string schemaErrorMessage;
					if (!TaskDeltaSchemaValidator::ValidateRun(
						runId,
						orderedTaskDeltas,
						schemaErrorCode,
						schemaErrorMessage)) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = schemaErrorCode,
										.message = schemaErrorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					std::string deltasJson = "[";
					for (std::size_t i = 0; i < orderedTaskDeltas.size(); ++i) {
						if (i > 0) {
							deltasJson += ",";
						}
						deltasJson += SerializeTaskDeltaEntryJson(orderedTaskDeltas[i]);
					}
					deltasJson += "]";

					return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
						"\",\"taskDeltas\":" + deltasJson +
						",\"count\":" + std::to_string(orderedTaskDeltas.size()) + "}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.taskDeltas.clear",
				[&host](const protocol::RequestFrame& request) {
					const std::string runId =
						ExtractStringParam(request.paramsJson, "runId");
					std::size_t cleared = 0;
					if (runId.empty()) {
						cleared = host.m_taskDeltaRepository.Size();
						host.m_taskDeltaRepository.ClearAll();
					}
					else {
						cleared = host.m_taskDeltaRepository.Clear(runId) ? 1 : 0;
					}

					return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId.empty() ? "*" : runId) +
						"\",\"cleared\":" + std::to_string(cleared) +
						",\"remaining\":" + std::to_string(host.m_taskDeltaRepository.Size()) + "}");
				});

			host.m_dispatcher.Register(
				"chat.history",
				[&host](const protocol::RequestFrame& request) {
					const std::string requestedSessionKey =
						ExtractStringParam(request.paramsJson, "sessionKey");
					const std::string sessionKey =
						requestedSessionKey.empty() ? "main" : requestedSessionKey;
					const std::size_t requestedLimit =
						ExtractSizeParam(request.paramsJson, "limit").value_or(200);
					const auto historyIt = host.m_chatHistoryBySession.find(sessionKey);
					ChatHistoryPolicy historyPolicy;
					ChatHistoryPolicy::BuildParams historyParams;
					historyParams.requestedLimit = requestedLimit;
					if (historyIt != host.m_chatHistoryBySession.end()) {
						historyParams.history = historyIt->second;
					}

					const auto historyResult = historyPolicy.Build(historyParams);
					if (historyResult.placeholderCount > 0) {
						EmitTelemetryEvent(
							"gateway.chat.history.placeholder",
							std::string("{\"sessionKey\":") +
							JsonString(sessionKey) +
							",\"placeholderCount\":" +
							std::to_string(historyResult.placeholderCount) +
							"}");
					}

					return protocol::OkResponse(request, "{\"messages\":" +
						historyResult.messagesJson +
						",\"thinkingLevel\":\"normal\"}");
				});
		}


	} // namespace handlers::runtime

} // namespace blazeclaw::gateway
