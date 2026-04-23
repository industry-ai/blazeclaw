#pragma once

#include "GatewaySessionRegistry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

	struct GatewaySessionListFilters {
		std::optional<bool> active;
		std::string scope;
		std::optional<std::size_t> limit;
		std::optional<std::size_t> activeMinutes;
		bool includeGlobal = false;
		bool includeUnknown = false;
		bool includeDerivedTitles = false;
		bool includeLastMessage = false;
		std::string label;
		std::string spawnedBy;
		std::string agentId;
		std::string search;
	};

	struct GatewaySessionProjection {
		std::string id;
		std::string key;
		std::string scope;
		bool active = true;
		std::string kind;
		std::string label;
		std::string displayName;
		std::string derivedTitle;
		std::string lastMessagePreview;
		std::string modelProvider;
		std::string model;
		std::uint64_t contextTokens = 0;
		std::uint64_t totalTokens = 0;
		double estimatedCostUsd = 0.0;
		bool totalTokensFresh = false;
		std::string fallbackSource = "registry";
		std::string spawnedBy;
		std::string parentSessionKey;
		std::vector<std::string> childSessions;
		std::int64_t updatedAt = 0;
	};

	struct GatewaySessionPreviewPayload {
		std::string json;
		std::string fallbackSource;
		bool totalTokensFresh = false;
	};

	struct GatewaySessionUsagePayload {
		std::string json;
		std::string fallbackSource;
		bool totalTokensFresh = false;
	};

	class GatewaySessionUtilsService {
	public:
		[[nodiscard]] static std::vector<SessionEntry> ListMergedSessions();
		[[nodiscard]] static std::optional<SessionEntry> ResolveFreshestSessionAcrossStores(
			const std::string& requestedId);
		[[nodiscard]] static std::string CanonicalizeSessionId(const std::string& value);
		[[nodiscard]] static std::vector<std::string> FindSessionIdsIgnoreCase(
			const std::vector<SessionEntry>& sessions,
			const std::string& targetId);
		[[nodiscard]] static std::optional<SessionEntry> ResolveFreshestSessionMatch(
			const std::vector<SessionEntry>& sessions,
			const std::vector<std::string>& candidateIds);
		[[nodiscard]] static std::string BuildSessionListPayload(
			const std::vector<SessionEntry>& sessions,
			const GatewaySessionListFilters& filters,
			const std::string& defaultModelProvider = "seed",
			const std::string& defaultModel = "default");
		[[nodiscard]] static GatewaySessionPreviewPayload BuildSessionPreviewPayload(
			const SessionEntry& session,
			const std::string& defaultModelProvider,
			const std::string& defaultModel);
		[[nodiscard]] static GatewaySessionUsagePayload BuildSessionUsagePayload(
			const SessionEntry& session,
			const std::string& defaultModelProvider,
			const std::string& defaultModel,
			const std::string& startDate,
			const std::string& endDate,
			bool openClawEnvelope);

	private:
		[[nodiscard]] static std::string BuildSessionProjectionJson(const GatewaySessionProjection& projection);
		[[nodiscard]] static GatewaySessionProjection BuildSessionProjection(
			const SessionEntry& session,
			const std::string& defaultModelProvider,
			const std::string& defaultModel);
	};

} // namespace blazeclaw::gateway
