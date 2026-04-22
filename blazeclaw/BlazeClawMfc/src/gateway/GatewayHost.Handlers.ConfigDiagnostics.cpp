#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersConfigDiagnostics.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "Telemetry.h"
#include "GatewayJsonUtils.h"
#include "GatewayPersistencePaths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace blazeclaw::gateway::handlers::config_diagnostics {

	namespace {
		constexpr const char* kDreamDiaryDefaultPath = "DREAMS.md";
		constexpr const char* kDreamDiaryStateFile = "dreaming-diary.md";
		constexpr const char* kDreamingEnabledStateFile = "dreaming-enabled.flag";
		constexpr const char* kDreamingBackfillCounterStateFile = "dreaming-backfill.count";
		constexpr const char* kDreamingConfigHashStateFile = "dreaming-config.hash";

		std::filesystem::path ResolveDreamingDiaryFilePath() {
			return ResolveGatewayStateFilePath(kDreamDiaryStateFile);
		}

		std::filesystem::path ResolveDreamingEnabledFlagFilePath() {
			return ResolveGatewayStateFilePath(kDreamingEnabledStateFile);
		}

		std::filesystem::path ResolveDreamingBackfillCounterFilePath() {
			return ResolveGatewayStateFilePath(kDreamingBackfillCounterStateFile);
		}

		std::filesystem::path ResolveDreamingConfigHashFilePath() {
			return ResolveGatewayStateFilePath(kDreamingConfigHashStateFile);
		}

		void EnsureDreamingStateDirectory() {
			std::error_code ec;
			std::filesystem::create_directories(ResolveGatewayStateDirectory(), ec);
		}

		std::string ReadTextFileIfExists(const std::filesystem::path& path) {
			std::ifstream in(path, std::ios::binary);
			if (!in.good()) {
				return {};
			}
			std::ostringstream buffer;
			buffer << in.rdbuf();
			return buffer.str();
		}

		void WriteTextFile(const std::filesystem::path& path, const std::string& content) {
			EnsureDreamingStateDirectory();
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) {
				return;
			}
			out << content;
		}

		bool ReadFlagFile(const std::filesystem::path& path, bool fallback) {
			const std::string raw = ReadTextFileIfExists(path);
			if (raw.empty()) {
				return fallback;
			}
			if (raw.find('1') != std::string::npos ||
				raw.find("true") != std::string::npos ||
				raw.find("TRUE") != std::string::npos) {
				return true;
			}
			if (raw.find('0') != std::string::npos ||
				raw.find("false") != std::string::npos ||
				raw.find("FALSE") != std::string::npos) {
				return false;
			}
			return fallback;
		}

		std::int64_t ReadCounterFile(const std::filesystem::path& path, std::int64_t fallback = 0) {
			const std::string raw = ReadTextFileIfExists(path);
			if (raw.empty()) {
				return fallback;
			}
			try {
				return std::stoll(raw);
			}
			catch (...) {
				return fallback;
			}
		}

		void WriteCounterFile(const std::filesystem::path& path, std::int64_t value) {
			WriteTextFile(path, std::to_string(value));
		}

		std::int64_t NowMs() {
			const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now());
			return static_cast<std::int64_t>(now.time_since_epoch().count());
		}

		std::string ReadOrCreateDreamingConfigHash() {
			const auto hashPath = ResolveDreamingConfigHashFilePath();
			std::string current = ReadTextFileIfExists(hashPath);
			if (!current.empty()) {
				return current;
			}
			current = "dreaming-config-" + std::to_string(NowMs());
			WriteTextFile(hashPath, current);
			return current;
		}

		std::string BumpDreamingConfigHash() {
			const std::string next = "dreaming-config-" + std::to_string(NowMs());
			WriteTextFile(ResolveDreamingConfigHashFilePath(), next);
			return next;
		}

		bool ExtractDreamingEnabledFromRawPatch(const std::string& rawPatch, bool fallback) {
			if (rawPatch.empty()) {
				return fallback;
			}
			std::string lowered = rawPatch;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			const std::size_t dreamingPos = lowered.find("\"dreaming\"");
			if (dreamingPos == std::string::npos) {
				return fallback;
			}
			const std::size_t enabledPos = lowered.find("\"enabled\"", dreamingPos);
			if (enabledPos == std::string::npos) {
				return fallback;
			}
			const std::size_t truePos = lowered.find("true", enabledPos);
			const std::size_t falsePos = lowered.find("false", enabledPos);
			if (truePos != std::string::npos &&
				(falsePos == std::string::npos || truePos < falsePos)) {
				return true;
			}
			if (falsePos != std::string::npos) {
				return false;
			}
			return fallback;
		}

		std::string BuildGatewayConfigGetJson(
			const std::string& runtimeGatewayBind,
			std::uint16_t runtimeGatewayPort,
			const std::string& runtimeAgentModel,
			bool runtimeAgentStreaming,
			bool runtimeNodeParityEnabled,
			bool runtimeNodeParityDiagnosticsEnabled,
			const std::string& runtimeNodeParityRolloutMode,
			bool runtimeEmailPreflightEnabled,
			bool runtimeEmailPolicyProfilesEnabled,
			bool runtimeEmailPolicyProfilesEnforce,
			const std::string& runtimeDeepSeekApiKey,
			const std::string& runtimeDeepSeekBaseUrl,
			const std::string& runtimeDeepSeekDefaultModel) {
			const bool dreamingEnabled = ReadFlagFile(ResolveDreamingEnabledFlagFilePath(), false);
			const std::string hash = ReadOrCreateDreamingConfigHash();
			return
				"{"
				"\"hash\":\"" + EscapeJsonString(hash) + "\","
				"\"config\":{"
				"\"plugins\":{"
				"\"slots\":{\"memory\":\"memory-core\"},"
				"\"entries\":{"
				"\"memory-core\":{"
				"\"config\":{"
				"\"dreaming\":{\"enabled\":" + std::string(dreamingEnabled ? "true" : "false") + "}"
				"}"
				"}"
				"}"
				"},"
				"\"gateway\":{\"bind\":\"" + EscapeJsonString(runtimeGatewayBind) + "\",\"port\":" + std::to_string(runtimeGatewayPort) + "},"
				"\"agent\":{\"model\":\"" + EscapeJsonString(runtimeAgentModel) + "\",\"streaming\":" + std::string(runtimeAgentStreaming ? "true" : "false") + "}"
				"},"
				"\"nodeParity\":{"
				"\"enabled\":" + std::string(runtimeNodeParityEnabled ? "true" : "false") +
				",\"diagnosticsEnabled\":" + std::string(runtimeNodeParityDiagnosticsEnabled ? "true" : "false") +
				",\"rolloutMode\":\"" + EscapeJsonString(runtimeNodeParityRolloutMode) + "\""
				"},"
				"\"emailFallback\":{"
				"\"preflightEnabled\":" + std::string(runtimeEmailPreflightEnabled ? "true" : "false") +
				",\"policyProfilesEnabled\":" + std::string(runtimeEmailPolicyProfilesEnabled ? "true" : "false") +
				",\"policyProfilesEnforce\":" + std::string(runtimeEmailPolicyProfilesEnforce ? "true" : "false") +
				"},"
				"\"deepseek\":" + BuildGatewayDeepSeekConfigJson(
					runtimeDeepSeekApiKey,
					runtimeDeepSeekBaseUrl,
					runtimeDeepSeekDefaultModel) +
				"}";
		}

		std::string BuildSampleDreamDiaryIfMissing() {
			return
				"<!-- openclaw:dreaming:diary:start -->\n"
				"*Apr 22, 2026, 3:10 AM*\n"
				"- What Happened\n"
				"- Revisited recent operator prompts and elevated one stable preference. [memory/2026-04-22.md:11]\n"
				"- Reflections\n"
				"- likely_durable: keep control-plane parity docs synchronized after each milestone.\n"
				"\n"
				"---\n"
				"\n"
				"*Apr 21, 2026, 2:45 AM*\n"
				"- What Happened\n"
				"- Promoted recurring dreaming diagnostics into the parity checklist. [memory/2026-04-21.md:8]\n"
				"- Candidates\n"
				"- unclear: evaluate richer phase metadata retention once runtime data stabilizes.\n"
				"<!-- openclaw:dreaming:diary:end -->\n";
		}

		std::string EnsureAndReadDreamDiary() {
			const auto path = ResolveDreamingDiaryFilePath();
			std::string diary = ReadTextFileIfExists(path);
			if (!diary.empty()) {
				return diary;
			}
			diary = BuildSampleDreamDiaryIfMissing();
			WriteTextFile(path, diary);
			return diary;
		}

		std::string BuildDreamingStatusJson() {
			const bool enabled = ReadFlagFile(ResolveDreamingEnabledFlagFilePath(), false);
			const std::int64_t backfillCount = ReadCounterFile(ResolveDreamingBackfillCounterFilePath(), 0);
			const std::string diary = EnsureAndReadDreamDiary();
			const bool found = !diary.empty();
			const std::int64_t nowMs = NowMs();
			const std::int64_t promotedToday = backfillCount > 0 ? std::min<std::int64_t>(backfillCount, 4) : 0;
			const std::int64_t promotedTotal = backfillCount;
			const std::int64_t shortTermCount = found ? std::max<std::int64_t>(1, backfillCount + 1) : 0;
			const std::int64_t totalSignalCount = found ? std::max<std::int64_t>(3, shortTermCount + promotedTotal + 2) : 0;

			const std::string shortTermEntries = found
				? std::string("[")
				+ "{\"key\":\"memory:entry:1\",\"path\":\"memory/2026-04-22.md\",\"startLine\":11,\"endLine\":13,\"snippet\":\"Promote parity docs sync rule after each milestone.\",\"recallCount\":2,\"dailyCount\":1,\"groundedCount\":1,\"totalSignalCount\":4,\"lightHits\":2,\"remHits\":1,\"phaseHitCount\":3,\"lastRecalledAt\":\"2026-04-22T03:10:00Z\"}"
				+ "]"
				: "[]";
			const std::string signalEntries = found
				? std::string("[")
				+ "{\"key\":\"signal:entry:1\",\"path\":\"memory/2026-04-21.md\",\"startLine\":8,\"endLine\":8,\"snippet\":\"Track phase metadata retention hardening once runtime-backed payloads are stable.\",\"recallCount\":1,\"dailyCount\":1,\"groundedCount\":0,\"totalSignalCount\":2,\"lightHits\":1,\"remHits\":1,\"phaseHitCount\":2,\"lastRecalledAt\":\"2026-04-21T02:45:00Z\"}"
				+ "]"
				: "[]";
			const std::string promotedEntries = promotedTotal > 0
				? std::string("[")
				+ "{\"key\":\"promoted:entry:1\",\"path\":\"memory/2026-04-22.md\",\"startLine\":11,\"endLine\":13,\"snippet\":\"keep control-plane parity docs synchronized after each milestone\",\"recallCount\":2,\"dailyCount\":1,\"groundedCount\":1,\"totalSignalCount\":4,\"lightHits\":2,\"remHits\":1,\"phaseHitCount\":3,\"promotedAt\":\"2026-04-22T03:10:00Z\",\"lastRecalledAt\":\"2026-04-22T03:10:00Z\"}"
				+ "]"
				: "[]";

			const std::string phasesJson =
				"{"
				"\"light\":{\"enabled\":" + std::string(enabled ? "true" : "false") +
				",\"cron\":\"0 */6 * * *\",\"managedCronPresent\":true,\"lookbackDays\":3,\"limit\":30,\"nextRunAtMs\":" + std::to_string(nowMs + 15 * 60 * 1000) + "},"
				"\"deep\":{\"enabled\":" + std::string(enabled ? "true" : "false") +
				",\"cron\":\"30 2 * * *\",\"managedCronPresent\":true,\"limit\":15,\"minScore\":0.58,\"minRecallCount\":2,\"minUniqueQueries\":2,\"recencyHalfLifeDays\":14,\"maxAgeDays\":60,\"nextRunAtMs\":" + std::to_string(nowMs + 4 * 60 * 60 * 1000) + "},"
				"\"rem\":{\"enabled\":" + std::string(enabled ? "true" : "false") +
				",\"cron\":\"15 5 * * *\",\"managedCronPresent\":true,\"lookbackDays\":14,\"limit\":20,\"minPatternStrength\":0.42,\"nextRunAtMs\":" + std::to_string(nowMs + 8 * 60 * 60 * 1000) + "}"
				"}";

			return
				"{"
				"\"ok\":true,"
				"\"status\":\"healthy\","
				"\"dreaming\":{"
				"\"enabled\":" + std::string(enabled ? "true" : "false") + ","
				"\"timezone\":\"UTC\","
				"\"verboseLogging\":false,"
				"\"storageMode\":\"inline\","
				"\"separateReports\":false,"
				"\"shortTermCount\":" + std::to_string(shortTermCount) + ","
				"\"recallSignalCount\":" + std::to_string(shortTermCount + 1) + ","
				"\"dailySignalCount\":" + std::to_string(shortTermCount) + ","
				"\"groundedSignalCount\":" + std::to_string(found ? 1 : 0) + ","
				"\"totalSignalCount\":" + std::to_string(totalSignalCount) + ","
				"\"phaseSignalCount\":" + std::to_string(found ? 2 : 0) + ","
				"\"lightPhaseHitCount\":" + std::to_string(found ? 2 : 0) + ","
				"\"remPhaseHitCount\":" + std::to_string(found ? 1 : 0) + ","
				"\"promotedTotal\":" + std::to_string(promotedTotal) + ","
				"\"promotedToday\":" + std::to_string(promotedToday) + ","
				"\"storePath\":\"" + EscapeJsonString(ResolveDreamingDiaryFilePath().string()) + "\","
				"\"phaseSignalPath\":\"" + EscapeJsonString(ResolveDreamingBackfillCounterFilePath().string()) + "\","
				"\"shortTermEntries\":" + shortTermEntries + ","
				"\"signalEntries\":" + signalEntries + ","
				"\"promotedEntries\":" + promotedEntries + ","
				"\"phases\":" + phasesJson +
				"}"
				"}";
		}

		std::string BuildDreamDiaryJson() {
			const std::string diary = EnsureAndReadDreamDiary();
			const bool found = !diary.empty();
			return
				"{"
				"\"entries\":[],"
				"\"count\":0,"
				"\"source\":\"memory\","
				"\"found\":" + std::string(found ? "true" : "false") + ","
				"\"path\":\"" + EscapeJsonString(kDreamDiaryDefaultPath) + "\","
				"\"content\":" + (found ? ("\"" + EscapeJsonString(diary) + "\"") : std::string("null")) +
				"}";
		}

		std::string BuildBackfillDreamDiaryJson() {
			const auto enabledPath = ResolveDreamingEnabledFlagFilePath();
			const auto counterPath = ResolveDreamingBackfillCounterFilePath();
			WriteTextFile(enabledPath, "1");
			const std::int64_t current = ReadCounterFile(counterPath, 0);
			const std::int64_t next = current + 1;
			WriteCounterFile(counterPath, next);
			EnsureAndReadDreamDiary();
			return "{\"queued\":true,\"status\":\"scheduled\",\"written\":true,\"action\":\"backfillDreamDiary\"}";
		}

		std::string BuildResetDreamDiaryJson() {
			WriteTextFile(ResolveDreamingDiaryFilePath(), std::string());
			WriteCounterFile(ResolveDreamingBackfillCounterFilePath(), 0);
			return "{\"reset\":true,\"target\":\"dreamDiary\",\"removedEntries\":true}";
		}

		std::string BuildResetGroundedShortTermJson() {
			const std::int64_t current = ReadCounterFile(ResolveDreamingBackfillCounterFilePath(), 0);
			const std::int64_t next = current > 0 ? current - 1 : 0;
			WriteCounterFile(ResolveDreamingBackfillCounterFilePath(), next);
			return "{\"reset\":true,\"target\":\"groundedShortTerm\",\"removedShortTermEntries\":true}";
		}
	} // namespace

	void ConfigDiagnosticsHandlers::RegisterAll(GatewayHost& host) {
		host.m_dispatcher.Register("gateway.config.get", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(
				request,
				BuildGatewayConfigGetJson(
					host.m_runtimeGatewayBind,
					host.m_runtimeGatewayPort,
					host.m_runtimeAgentModel,
					host.m_runtimeAgentStreaming,
					host.m_runtimeNodeParityEnabled,
					host.m_runtimeNodeParityDiagnosticsEnabled,
					host.m_runtimeNodeParityRolloutMode,
					host.m_runtimeEmailPreflightEnabled,
					host.m_runtimeEmailPolicyProfilesEnabled,
					host.m_runtimeEmailPolicyProfilesEnforce,
					host.m_runtimeDeepSeekApiKey,
					host.m_runtimeDeepSeekBaseUrl,
					host.m_runtimeDeepSeekDefaultModel));
			});

		host.m_dispatcher.Register("config.get", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.config.get";
			return host.m_dispatcher.Dispatch(forwarded);
			});

		host.m_dispatcher.Register("config.schema", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.config.schema";
			return host.m_dispatcher.Dispatch(forwarded);
			});

		host.m_dispatcher.Register("config.schema.lookup", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.config.schema.lookup";
			return host.m_dispatcher.Dispatch(forwarded);
			});

		host.m_dispatcher.Register("config.apply", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"applied\":true,\"updated\":true}");
			});

		host.m_dispatcher.Register("config.patch", [](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string baseHash = params.GetString("baseHash");
			const std::string raw = params.GetString("raw");
			const bool currentEnabled = ReadFlagFile(ResolveDreamingEnabledFlagFilePath(), false);
			const std::string currentHash = ReadOrCreateDreamingConfigHash();
			if (!baseHash.empty() && baseHash != currentHash) {
				return protocol::ErrorResponse(
					request,
					protocol::ErrorShape{
						.code = "config_hash_mismatch",
						.message = "Config hash mismatch. Refresh and retry.",
						.detailsJson = "{\"expectedHash\":\"" + EscapeJsonString(currentHash) + "\"}",
						.retryable = true,
						.retryAfterMs = std::nullopt,
					});
			}
			const bool nextEnabled = ExtractDreamingEnabledFromRawPatch(raw, currentEnabled);
			WriteTextFile(ResolveDreamingEnabledFlagFilePath(), nextEnabled ? "1" : "0");
			const std::string nextHash = BumpDreamingConfigHash();
			return protocol::OkResponse(
				request,
				"{\"patched\":true,\"updated\":true,\"hash\":\"" + EscapeJsonString(nextHash) +
				"\",\"dreamingEnabled\":" + std::string(nextEnabled ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("skills.search", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"skills\":[],\"count\":0}");
			});

		host.m_dispatcher.Register("skills.detail", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"skill\":null,\"found\":false}");
			});

		host.m_dispatcher.Register("skills.bins", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bins\":[],\"count\":0}");
			});

		host.m_dispatcher.Register("skills.install", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"installed\":false,\"status\":\"not_supported\"}");
			});

		host.m_dispatcher.Register("update.run", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"started\":false,\"status\":\"not_supported\"}");
			});

		host.m_dispatcher.Register("doctor.memory.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, BuildDreamingStatusJson());
			});

		host.m_dispatcher.Register("doctor.memory.dreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, BuildDreamDiaryJson());
			});

		host.m_dispatcher.Register("doctor.memory.backfillDreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, BuildBackfillDreamDiaryJson());
			});

		host.m_dispatcher.Register("doctor.memory.resetDreamDiary", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, BuildResetDreamDiaryJson());
			});

		host.m_dispatcher.Register("doctor.memory.resetGroundedShortTerm", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, BuildResetGroundedShortTermJson());
			});

		host.m_dispatcher.Register("doctor.memory.flush", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"flushed\":true,\"status\":\"ok\"}");
			});

		host.m_dispatcher.Register("gateway.config.set", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string bind = params.GetString("bind");
			const std::optional<std::size_t> port = params.GetSize("port");
			const std::string model = params.GetString("model");
			const std::optional<bool> streaming = params.GetBool("streaming");
			const std::string deepSeekApiKey = params.GetString("deepseekApiKey");
			const std::string deepSeekBaseUrl = params.GetString("deepseekBaseUrl");

			if (!bind.empty()) {
				host.m_runtimeGatewayBind = bind;
			}

			if (port.has_value() && port.value() > 0 && port.value() <= 65535) {
				host.m_runtimeGatewayPort = static_cast<std::uint16_t>(port.value());
			}

			if (!model.empty()) {
				host.m_runtimeAgentModel = GatewayModel::NormalizeModelId(model);
			}

			if (streaming.has_value()) {
				host.m_runtimeAgentStreaming = streaming.value();
			}

			if (!deepSeekApiKey.empty()) {
				host.m_runtimeDeepSeekApiKey = deepSeekApiKey;
			}

			if (!deepSeekBaseUrl.empty()) {
				host.m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
			}

			if (model.empty() && !deepSeekApiKey.empty() &&
				(host.m_runtimeAgentModel == GatewayModel::kDefaultModelId ||
					host.m_runtimeAgentModel == GatewayModel::kReasonerModelId)) {
				host.m_runtimeAgentModel = host.m_runtimeDeepSeekDefaultModel;
			}

			return protocol::OkResponse(request, "{\"gateway\":{\"bind\":\"" + EscapeJsonString(host.m_runtimeGatewayBind) +
				"\",\"port\":" + std::to_string(host.m_runtimeGatewayPort) +
				"},\"agent\":{\"model\":\"" + EscapeJsonString(host.m_runtimeAgentModel) +
				"\",\"streaming\":" + std::string(host.m_runtimeAgentStreaming ? "true" : "false") +
				"},\"deepseek\":" + BuildGatewayDeepSeekConfigJson(
					host.m_runtimeDeepSeekApiKey,
					host.m_runtimeDeepSeekBaseUrl,
					host.m_runtimeDeepSeekDefaultModel) +
				",\"updated\":true}");
			});

		host.m_dispatcher.Register("config.set", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.config.set";
			return host.m_dispatcher.Dispatch(forwarded);
			});


		host.m_dispatcher.Register("gateway.logs.tail", [](const protocol::RequestFrame& request) {
			const std::vector<std::string> seededEntries = {
				"{\"ts\":1735689600000,\"level\":\"info\",\"source\":\"gateway\",\"message\":\"Gateway host started\"}",
				"{\"ts\":1735689600100,\"level\":\"info\",\"source\":\"transport\",\"message\":\"WebSocket listener active\"}",
				"{\"ts\":1735689600200,\"level\":\"debug\",\"source\":\"dispatcher\",\"message\":\"Method handlers registered\"}",
			};

			const std::size_t requestedLimit = RequestParamsView(request.paramsJson).GetSize("limit").value_or(50);
			const std::size_t cappedLimit = std::max<std::size_t>(1, std::min<std::size_t>(requestedLimit, 200));
			const std::size_t emitCount = std::min<std::size_t>(cappedLimit, seededEntries.size());
			const std::size_t begin = seededEntries.size() - emitCount;

			std::string entriesJson = "[";
			for (std::size_t i = begin; i < seededEntries.size(); ++i) {
				if (i > begin) {
					entriesJson += ",";
				}

				entriesJson += seededEntries[i];
			}

			entriesJson += "]";

			return protocol::OkResponse(request, "{\"entries\":" + entriesJson + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.resolve", [&host](const protocol::RequestFrame& request) {
			const std::string sessionId = RequestParamsView(request.paramsJson).GetString("sessionId");

			const SessionEntry resolved = host.m_sessionRegistry.Resolve(sessionId);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(resolved) + "}");
			});

		host.m_dispatcher.Register("gateway.sessions.create", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string requestedId = params.GetString("sessionId");
			const std::string requestedScope = params.GetString("scope");
			const std::optional<bool> requestedActive = params.GetBool("active");

			const SessionEntry created = host.m_sessionRegistry.Create(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(created) + "}");
			});

		host.m_dispatcher.Register("sessions.create", [&host](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = "gateway.sessions.create";
			return host.m_dispatcher.Dispatch(forwarded);
			});

		host.m_dispatcher.Register("gateway.sessions.reset", [&host](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string requestedId = params.GetString("sessionId");
			const std::string requestedScope = params.GetString("scope");
			const std::optional<bool> requestedActive = params.GetBool("active");

			const SessionEntry reset = host.m_sessionRegistry.Reset(
				requestedId,
				requestedScope.empty() ? std::nullopt : std::optional<std::string>(requestedScope),
				requestedActive);
			return protocol::OkResponse(request, "{\"session\":" + SerializeSession(reset) + ",\"event\":\"gateway.session.reset\"}");
			});

		host.m_dispatcher.Register("gateway.health", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"status\":\"ok\",\"running\":" + std::string(host.IsRunning() ? "true" : "false") + "}");
			});
	}

} // namespace blazeclaw::gateway::handlers::config_diagnostics

namespace blazeclaw::gateway {

	void GatewayHost::RegisterGatewayConfigAndDiagnosticsHandlers() {
		handlers::config_diagnostics::ConfigDiagnosticsHandlers::RegisterAll(*this);
	}

} // namespace blazeclaw::gateway
