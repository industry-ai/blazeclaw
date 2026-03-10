#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

	struct AgentEntry {
		std::string id;
		std::string name;
		bool active = false;
	};

	struct AgentFileEntry {
		std::string path;
		std::size_t size = 0;
		std::uint64_t updatedMs = 0;
	};

	class GatewayAgentRegistry {
	public:
		GatewayAgentRegistry();

		std::vector<AgentEntry> List() const;
		AgentEntry Get(const std::string& requestedId) const;
		AgentEntry Activate(const std::string& requestedId);
		AgentEntry Create(
			const std::string& requestedId,
			const std::optional<std::string>& requestedName = std::nullopt,
			std::optional<bool> requestedActive = std::nullopt);
		bool Delete(const std::string& requestedId, AgentEntry& removedAgent);
		AgentEntry Update(
			const std::string& requestedId,
			const std::optional<std::string>& requestedName = std::nullopt,
			std::optional<bool> requestedActive = std::nullopt);
		std::vector<AgentFileEntry> ListFiles(const std::string& requestedId) const;

	private:
		static std::string NormalizeAgentId(const std::string& value);
		static AgentEntry BuildDefaultAgent();

		std::unordered_map<std::string, AgentEntry> m_agents;
	};

} // namespace blazeclaw::gateway
