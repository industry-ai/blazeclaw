#include "pch.h"
#include "GatewaySessionUtilsService.h"

#include "GatewayJsonSerializers.h"
#include "GatewayJsonUtils.h"
#include "GatewayPersistencePaths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace blazeclaw::gateway {
	namespace {
		std::string Trim(const std::string& value) {
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}

			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}

			return value.substr(start, end - start);
		}

		std::string ToLower(const std::string& value) {
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

		bool ContainsCaseInsensitive(
			const std::string& haystack,
			const std::string& needle) {
			if (needle.empty()) {
				return true;
			}
			return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
		}

		std::int64_t NowEpochMs() {
			const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now());
			return static_cast<std::int64_t>(now.time_since_epoch().count());
		}

		std::uint64_t EstimateTokenCountFromText(const std::string& text) {
			if (text.empty()) {
				return 0;
			}
			return static_cast<std::uint64_t>((std::max<std::size_t>)(1, text.size() / 4));
		}

		std::string NormalizeSessionKeyForFileName(const std::string& sessionKey) {
			const std::string source = sessionKey.empty() ? "main" : sessionKey;
			std::string normalized;
			normalized.reserve(source.size());
			for (const unsigned char ch : source) {
				const bool isSafe =
					(ch >= 'a' && ch <= 'z') ||
					(ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9') ||
					ch == '-' ||
					ch == '_' ||
					ch == '.';
				normalized.push_back(isSafe ? static_cast<char>(ch) : '_');
			}
			return normalized.empty() ? std::string("main") : normalized;
		}

		struct TranscriptSnapshot {
			bool found = false;
			std::uint64_t messageCount = 0;
			std::uint64_t inputTokens = 0;
			std::uint64_t outputTokens = 0;
			std::uint64_t totalTokens = 0;
			std::uint64_t lastActiveMs = 0;
			std::string firstUserMessage;
			std::string lastMessagePreview;
		};

		TranscriptSnapshot ReadTranscriptSnapshot(const std::string& sessionId) {
			TranscriptSnapshot snapshot{};
			const std::filesystem::path transcriptPath =
				ResolveGatewayStateFilePath("chat-transcripts") /
				(NormalizeSessionKeyForFileName(sessionId) + ".jsonl");
			std::ifstream input(transcriptPath, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				return snapshot;
			}

			std::string line;
			while (std::getline(input, line)) {
				if (line.empty()) {
					continue;
				}
				snapshot.found = true;
				++snapshot.messageCount;
				std::string role;
				std::string text;
				json::FindStringField(line, "role", role);
				if (!json::FindStringField(line, "text", text)) {
					json::FindStringField(line, "message", text);
				}
				if (role == "user") {
					snapshot.inputTokens += EstimateTokenCountFromText(text);
					if (snapshot.firstUserMessage.empty() && !text.empty()) {
						snapshot.firstUserMessage = text;
					}
				}
				else {
					snapshot.outputTokens += EstimateTokenCountFromText(text);
				}
				if (!text.empty()) {
					snapshot.lastMessagePreview = text;
				}
				std::uint64_t timestamp = 0;
				if (json::FindUInt64Field(line, "timestamp", timestamp)) {
					snapshot.lastActiveMs = (std::max)(snapshot.lastActiveMs, timestamp);
				}
			}

			snapshot.totalTokens = snapshot.inputTokens + snapshot.outputTokens;
			if (snapshot.lastActiveMs == 0) {
				snapshot.lastActiveMs = static_cast<std::uint64_t>(NowEpochMs());
			}
			return snapshot;
		}

		double ResolveEstimatedCostUsd(
			const std::string& provider,
			const std::string& model,
			const std::uint64_t inputTokens,
			const std::uint64_t outputTokens) {
			// Conservative static fallback for parity migration phase; replace with provider catalog pricing in later passes.
			double inputPer1k = 0.0002;
			double outputPer1k = 0.0006;
			const std::string loweredProvider = ToLower(provider);
			const std::string loweredModel = ToLower(model);
			if (loweredProvider.find("deepseek") != std::string::npos ||
				loweredModel.find("deepseek") != std::string::npos) {
				inputPer1k = 0.00014;
				outputPer1k = 0.00028;
			}
			return (static_cast<double>(inputTokens) / 1000.0 * inputPer1k) +
				(static_cast<double>(outputTokens) / 1000.0 * outputPer1k);
		}

		std::string ResolveDerivedTitle(
			const std::string& sessionId,
			const TranscriptSnapshot& snapshot) {
			if (!snapshot.firstUserMessage.empty()) {
				std::string title = snapshot.firstUserMessage;
				if (title.size() > 60) {
					title = title.substr(0, 60);
				}
				return title;
			}
			return "Session " + sessionId;
		}
	} // namespace

	std::string GatewaySessionUtilsService::CanonicalizeSessionId(const std::string& value) {
		const std::string trimmed = Trim(value);
		if (trimmed.empty()) {
			return "main";
		}
		return ToLower(trimmed);
	}

	std::vector<std::string> GatewaySessionUtilsService::FindSessionIdsIgnoreCase(
		const std::vector<SessionEntry>& sessions,
		const std::string& targetId) {
		const std::string loweredTarget = ToLower(CanonicalizeSessionId(targetId));
		std::vector<std::string> matches;
		for (const auto& session : sessions) {
			if (ToLower(CanonicalizeSessionId(session.id)) == loweredTarget) {
				matches.push_back(session.id);
			}
		}
		return matches;
	}

	std::optional<SessionEntry> GatewaySessionUtilsService::ResolveFreshestSessionMatch(
		const std::vector<SessionEntry>& sessions,
		const std::vector<std::string>& candidateIds) {
		std::optional<SessionEntry> chosen;
		std::unordered_set<std::string> normalizedCandidates;
		for (const auto& candidate : candidateIds) {
			normalizedCandidates.insert(ToLower(CanonicalizeSessionId(candidate)));
		}
		if (normalizedCandidates.empty()) {
			return std::nullopt;
		}

		for (const auto& session : sessions) {
			const std::string normalized = ToLower(CanonicalizeSessionId(session.id));
			if (!normalizedCandidates.contains(normalized)) {
				continue;
			}
			if (!chosen.has_value()) {
				chosen = session;
				continue;
			}
			if (session.active && !chosen->active) {
				chosen = session;
				continue;
			}
			const std::uint64_t chosenUpdatedAt = chosen->updatedAt;
			if (session.updatedAt > chosenUpdatedAt) {
				chosen = session;
				continue;
			}
			if (session.updatedAt == chosenUpdatedAt && session.id < chosen->id) {
				chosen = session;
			}
		}
		return chosen;
	}

	GatewaySessionProjection GatewaySessionUtilsService::BuildSessionProjection(const SessionEntry& session) {
		GatewaySessionProjection projection{};
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		projection.id = session.id;
		projection.key = session.id;
		projection.scope = session.scope;
		projection.active = session.active;
		projection.kind = (session.id == "global" || session.id == "unknown") ? session.id : "direct";
		projection.label = session.id;
		projection.displayName = "Session " + session.id;
		projection.derivedTitle = ResolveDerivedTitle(session.id, transcript);
		projection.lastMessagePreview = transcript.lastMessagePreview;
		projection.modelProvider = "deepseek";
		projection.model = "deepseek/deepseek-chat";
		projection.contextTokens = 128000;
		projection.totalTokens = transcript.totalTokens;
		projection.totalTokensFresh = transcript.found && transcript.totalTokens > 0;
		projection.estimatedCostUsd = ResolveEstimatedCostUsd(
			projection.modelProvider,
			projection.model,
			transcript.inputTokens,
			transcript.outputTokens);
		projection.fallbackSource = transcript.found ? "transcript" : "registry";
		projection.updatedAt = transcript.found
			? static_cast<std::int64_t>(transcript.lastActiveMs)
			: NowEpochMs();
		return projection;
	}

	std::string GatewaySessionUtilsService::BuildSessionProjectionJson(const GatewaySessionProjection& projection) {
		std::ostringstream out;
		out << "{"
			<< "\"id\":\"" << EscapeJsonString(projection.id) << "\","
			<< "\"key\":\"" << EscapeJsonString(projection.key) << "\","
			<< "\"scope\":\"" << EscapeJsonString(projection.scope) << "\","
			<< "\"active\":" << (projection.active ? "true" : "false") << ","
			<< "\"kind\":\"" << EscapeJsonString(projection.kind) << "\","
			<< "\"label\":\"" << EscapeJsonString(projection.label) << "\","
			<< "\"displayName\":\"" << EscapeJsonString(projection.displayName) << "\","
			<< "\"updatedAt\":" << projection.updatedAt;

		if (!projection.derivedTitle.empty()) {
			out << ",\"derivedTitle\":\"" << EscapeJsonString(projection.derivedTitle) << "\"";
		}
		if (!projection.lastMessagePreview.empty()) {
			out << ",\"lastMessagePreview\":\"" << EscapeJsonString(projection.lastMessagePreview) << "\"";
		}
		out << ",\"modelProvider\":\"" << EscapeJsonString(projection.modelProvider) << "\""
			<< ",\"model\":\"" << EscapeJsonString(projection.model) << "\""
			<< ",\"contextTokens\":" << projection.contextTokens
			<< ",\"totalTokens\":" << projection.totalTokens
			<< ",\"totalTokensFresh\":" << (projection.totalTokensFresh ? "true" : "false")
			<< ",\"estimatedCostUsd\":" << projection.estimatedCostUsd
			<< ",\"fallbackSource\":\"" << EscapeJsonString(projection.fallbackSource) << "\"";
		if (!projection.spawnedBy.empty()) {
			out << ",\"spawnedBy\":\"" << EscapeJsonString(projection.spawnedBy) << "\"";
		}
		if (!projection.parentSessionKey.empty()) {
			out << ",\"parentSessionKey\":\"" << EscapeJsonString(projection.parentSessionKey) << "\"";
		}
		if (!projection.childSessions.empty()) {
			out << ",\"childSessions\":" << SerializeStringArray(projection.childSessions);
		}

		out << "}";
		return out.str();
	}

	std::string GatewaySessionUtilsService::BuildSessionListPayload(
		const std::vector<SessionEntry>& sessions,
		const GatewaySessionListFilters& filters) {
		const std::int64_t nowMs = NowEpochMs();
		const std::int64_t activeCutoffMs = filters.activeMinutes.has_value()
			? nowMs - static_cast<std::int64_t>(filters.activeMinutes.value()) * 60 * 1000
			: (std::numeric_limits<std::int64_t>::min)();
		std::vector<GatewaySessionProjection> projected;
		projected.reserve(sessions.size());
		for (const auto& session : sessions) {
			const std::string canonicalId = CanonicalizeSessionId(session.id);
			if (!filters.includeGlobal && canonicalId == "global") {
				continue;
			}
			if (!filters.includeUnknown && canonicalId == "unknown") {
				continue;
			}
			if (filters.active.has_value() && session.active != filters.active.value()) {
				continue;
			}
			if (!filters.scope.empty() && session.scope != filters.scope) {
				continue;
			}
			if (!filters.label.empty() && !ContainsCaseInsensitive(session.id, filters.label)) {
				continue;
			}
			if (!filters.spawnedBy.empty() && !ContainsCaseInsensitive(session.id, filters.spawnedBy)) {
				continue;
			}
			if (!filters.agentId.empty()) {
				const std::string expectedAgentPrefix = "agent:" + ToLower(filters.agentId) + ":";
				if (ToLower(canonicalId).find(expectedAgentPrefix) != 0) {
					continue;
				}
			}
			if (!filters.search.empty() &&
				!ContainsCaseInsensitive(session.id, filters.search) &&
				!ContainsCaseInsensitive(session.scope, filters.search)) {
				continue;
			}

			GatewaySessionProjection row = BuildSessionProjection(session);
			if (!filters.includeDerivedTitles) {
				row.derivedTitle.clear();
			}
			if (!filters.includeLastMessage) {
				row.lastMessagePreview.clear();
			}
			if (row.updatedAt < activeCutoffMs) {
				continue;
			}
			projected.push_back(std::move(row));
		}

		std::sort(
			projected.begin(),
			projected.end(),
			[](const GatewaySessionProjection& left, const GatewaySessionProjection& right) {
				if (left.updatedAt != right.updatedAt) {
					return left.updatedAt > right.updatedAt;
				}
				return left.id < right.id;
			});

		if (filters.limit.has_value()) {
			const std::size_t capped = (std::max<std::size_t>)(1, filters.limit.value());
			if (projected.size() > capped) {
				projected.resize(capped);
			}
		}

		std::ostringstream sessionsJson;
		sessionsJson << "[";
		bool first = true;
		std::string activeSessionId = "none";
		for (const auto& row : projected) {
			if (!first) {
				sessionsJson << ",";
			}
			first = false;
			sessionsJson << BuildSessionProjectionJson(row);
			if (activeSessionId == "none" && row.active) {
				activeSessionId = row.id;
			}
		}
		sessionsJson << "]";

		std::ostringstream payload;
		payload
			<< "{"
			<< "\"sessions\":" << sessionsJson.str() << ","
			<< "\"count\":" << projected.size() << ","
			<< "\"activeSessionId\":\"" << EscapeJsonString(activeSessionId) << "\""
			<< "}";
		return payload.str();
	}

	GatewaySessionPreviewPayload GatewaySessionUtilsService::BuildSessionPreviewPayload(
		const SessionEntry& session,
		const std::string& defaultModelProvider,
		const std::string& defaultModel) {
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		const std::string title = ResolveDerivedTitle(session.id, transcript);
		const std::string modelProvider = defaultModelProvider.empty() ? "deepseek" : defaultModelProvider;
		const std::string model = defaultModel.empty() ? "deepseek/deepseek-chat" : defaultModel;
		std::ostringstream payload;
		payload
			<< "{"
			<< "\"session\":" << SerializeSession(session) << ","
			<< "\"title\":\"" << EscapeJsonString(title) << "\","
			<< "\"lastMessagePreview\":\"" << EscapeJsonString(transcript.lastMessagePreview) << "\","
			<< "\"hasMessages\":" << (transcript.messageCount > 0 ? "true" : "false") << ","
			<< "\"unread\":0,"
			<< "\"usage\":{\"messages\":" << transcript.messageCount
			<< ",\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
			<< "},"
			<< "\"modelProvider\":\"" << EscapeJsonString(modelProvider) << "\","
			<< "\"model\":\"" << EscapeJsonString(model) << "\","
			<< "\"contextTokens\":128000,"
			<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\""
			<< "}";
		return GatewaySessionPreviewPayload{
			payload.str(),
			transcript.found ? "transcript" : "registry",
			transcript.found && transcript.totalTokens > 0
		};
	}

	GatewaySessionUsagePayload GatewaySessionUtilsService::BuildSessionUsagePayload(
		const SessionEntry& session,
		const std::string& defaultModelProvider,
		const std::string& defaultModel,
		const std::string& startDate,
		const std::string& endDate,
		bool openClawEnvelope) {
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		const std::string modelProvider = defaultModelProvider.empty() ? "deepseek" : defaultModelProvider;
		const std::string model = defaultModel.empty() ? "deepseek/deepseek-chat" : defaultModel;
		const double totalCost = ResolveEstimatedCostUsd(
			modelProvider,
			model,
			transcript.inputTokens,
			transcript.outputTokens);
		const std::uint64_t lastActiveMs = transcript.lastActiveMs > 0
			? transcript.lastActiveMs
			: static_cast<std::uint64_t>(NowEpochMs());

		std::ostringstream payload;
		if (!openClawEnvelope) {
			payload
				<< "{"
				<< "\"sessionId\":\"" << EscapeJsonString(session.id) << "\","
				<< "\"messages\":" << transcript.messageCount << ","
				<< "\"tokens\":{\"input\":" << transcript.inputTokens
				<< ",\"output\":" << transcript.outputTokens
				<< ",\"total\":" << transcript.totalTokens
				<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
				<< "},"
				<< "\"modelProvider\":\"" << EscapeJsonString(modelProvider) << "\","
				<< "\"model\":\"" << EscapeJsonString(model) << "\","
				<< "\"estimatedCostUsd\":" << totalCost << ","
				<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\","
				<< "\"lastActiveMs\":" << lastActiveMs
				<< "}";
			return GatewaySessionUsagePayload{
				payload.str(),
				transcript.found ? "transcript" : "registry",
				transcript.found && transcript.totalTokens > 0
			};
		}

		const std::string usageStart = startDate.empty() ? "2026-01-01" : startDate;
		const std::string usageEnd = endDate.empty() ? usageStart : endDate;
		payload
			<< "{"
			<< "\"updatedAt\":" << lastActiveMs << ","
			<< "\"startDate\":\"" << EscapeJsonString(usageStart) << "\","
			<< "\"endDate\":\"" << EscapeJsonString(usageEnd) << "\","
			<< "\"sessions\":[{"
			<< "\"key\":\"" << EscapeJsonString(session.id) << "\","
			<< "\"label\":\"Session " << EscapeJsonString(session.id) << "\","
			<< "\"sessionId\":\"" << EscapeJsonString(session.id) << "\","
			<< "\"updatedAt\":" << lastActiveMs << ","
			<< "\"modelProvider\":\"" << EscapeJsonString(modelProvider) << "\","
			<< "\"model\":\"" << EscapeJsonString(model) << "\","
			<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\","
			<< "\"usage\":{\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"cacheRead\":0,\"cacheWrite\":0"
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
			<< ",\"totalCost\":" << totalCost
			<< ",\"messages\":" << transcript.messageCount << "}}],"
			<< "\"totals\":{\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"cacheRead\":0,\"cacheWrite\":0"
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalCost\":" << totalCost
			<< ",\"inputCost\":" << totalCost * 0.33
			<< ",\"outputCost\":" << totalCost * 0.67
			<< ",\"cacheReadCost\":0.0,\"cacheWriteCost\":0.0,\"missingCostEntries\":0}"
			<< "}";
		return GatewaySessionUsagePayload{
			payload.str(),
			transcript.found ? "transcript" : "registry",
			transcript.found && transcript.totalTokens > 0
		};
	}

} // namespace blazeclaw::gateway
