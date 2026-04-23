#include "pch.h"
#include "GatewaySessionUtilsService.h"

#include "GatewayJsonSerializers.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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
			if (session.id < chosen->id) {
				chosen = session;
			}
		}
		return chosen;
	}

	GatewaySessionProjection GatewaySessionUtilsService::BuildSessionProjection(const SessionEntry& session) {
		GatewaySessionProjection projection{};
		projection.id = session.id;
		projection.key = session.id;
		projection.scope = session.scope;
		projection.active = session.active;
		projection.kind = (session.id == "global" || session.id == "unknown") ? session.id : "direct";
		projection.label = session.id;
		projection.displayName = "Session " + session.id;
		projection.derivedTitle = "Session " + session.id;
		projection.lastMessagePreview = "";
		projection.updatedAt = NowEpochMs();
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
			: std::numeric_limits<std::int64_t>::min();
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
			const std::size_t capped = std::max<std::size_t>(1, filters.limit.value());
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

} // namespace blazeclaw::gateway
