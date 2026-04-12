#pragma once

#include "../config/ConfigModels.h"
#include "../gateway/GatewayHost.h"

#include <deque>
#include <mutex>

namespace blazeclaw::core {

	class ConfigSchemaService {
	public:
		ConfigSchemaService() = default;

		void Invalidate();

		[[nodiscard]] blazeclaw::gateway::ConfigSchemaGatewayState BuildGatewayState(
			const blazeclaw::config::AppConfig& config,
			const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState) const;

		[[nodiscard]] std::optional<blazeclaw::gateway::ConfigSchemaGatewayLookupResult> Lookup(
			const blazeclaw::gateway::ConfigSchemaGatewayState& state,
			const std::string& path) const;

	private:
		struct CacheEntry {
			std::string key;
			blazeclaw::gateway::ConfigSchemaGatewayState state;
		};

		[[nodiscard]] static std::string BuildCacheKey(
			const blazeclaw::config::AppConfig& config,
			const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState);

		mutable std::mutex m_cacheMutex;
		mutable std::deque<CacheEntry> m_cacheEntries;
	};

} // namespace blazeclaw::core
