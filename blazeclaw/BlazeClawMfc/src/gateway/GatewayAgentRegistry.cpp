#include "pch.h"
#include "GatewayAgentRegistry.h"

#include <algorithm>

namespace blazeclaw::gateway {

	GatewayAgentRegistry::GatewayAgentRegistry() {
		const AgentEntry defaultAgent = BuildDefaultAgent();
		m_agents.insert_or_assign(defaultAgent.id, defaultAgent);
	}

	std::vector<AgentEntry> GatewayAgentRegistry::List() const {
		std::vector<AgentEntry> output;
		output.reserve(m_agents.size());

		for (const auto& [_, agent] : m_agents) {
			output.push_back(agent);
		}

		std::sort(output.begin(), output.end(), [](const AgentEntry& left, const AgentEntry& right) {
			return left.id < right.id;
			});

		return output;
	}

	AgentEntry GatewayAgentRegistry::Get(const std::string& requestedId) const {
		const std::string id = NormalizeAgentId(requestedId);
		const auto it = m_agents.find(id);
		if (it != m_agents.end()) {
			return it->second;
		}

		return BuildDefaultAgent();
	}

	AgentEntry GatewayAgentRegistry::Activate(const std::string& requestedId) {
		const std::string id = NormalizeAgentId(requestedId);

		for (auto& [_, agent] : m_agents) {
			agent.active = false;
		}

		AgentEntry activated = Get(id);
		activated.active = true;
		m_agents.insert_or_assign(activated.id, activated);
		return activated;
	}

	AgentEntry GatewayAgentRegistry::Create(
		const std::string& requestedId,
		const std::optional<std::string>& requestedName,
		std::optional<bool> requestedActive) {
		const std::string id = NormalizeAgentId(requestedId);
		const bool active = requestedActive.value_or(id == "default");

		if (active) {
			for (auto& [_, agent] : m_agents) {
				agent.active = false;
			}
		}

		AgentEntry created{
			.id = id,
			.name = requestedName.has_value() && !requestedName.value().empty()
				? requestedName.value()
				: (id == "default" ? "Default Agent" : "Agent " + id),
			.active = active,
		};

		m_agents.insert_or_assign(created.id, created);
		return created;
	}

	bool GatewayAgentRegistry::Delete(const std::string& requestedId, AgentEntry& removedAgent) {
		const std::string id = NormalizeAgentId(requestedId);
		if (id == "default") {
			removedAgent = Get(id);
			return false;
		}

		const auto it = m_agents.find(id);
		if (it == m_agents.end()) {
			removedAgent = AgentEntry{
				.id = id,
				.name = "Agent " + id,
				.active = false,
			};
			return false;
		}

		const bool wasActive = it->second.active;
		removedAgent = it->second;
		removedAgent.active = false;
		m_agents.erase(it);

		if (wasActive) {
			Activate("default");
		}

		return true;
	}

	AgentEntry GatewayAgentRegistry::Update(
		const std::string& requestedId,
		const std::optional<std::string>& requestedName,
		std::optional<bool> requestedActive) {
		const std::string id = NormalizeAgentId(requestedId);
		AgentEntry updated = Get(id);
		updated.id = id;

		if (requestedName.has_value() && !requestedName.value().empty()) {
			updated.name = requestedName.value();
		}

		if (requestedActive.has_value()) {
			if (requestedActive.value()) {
				for (auto& [_, agent] : m_agents) {
					agent.active = false;
				}
			}

			updated.active = requestedActive.value();
		}

		m_agents.insert_or_assign(updated.id, updated);
		return updated;
	}

	std::vector<AgentFileEntry> GatewayAgentRegistry::ListFiles(const std::string& requestedId) const {
		const AgentEntry agent = Get(requestedId);
		const std::string prefix = agent.id;

		return {
			AgentFileEntry{.path = "agents/" + prefix + "/profile.json", .size = 512, .updatedMs = 1735689600000},
			AgentFileEntry{.path = "agents/" + prefix + "/memory.txt", .size = 2048, .updatedMs = 1735689605000},
		};
	}

	std::string GatewayAgentRegistry::NormalizeAgentId(const std::string& value) {
		if (value.empty()) {
			return "default";
		}

		return value;
	}

	AgentEntry GatewayAgentRegistry::BuildDefaultAgent() {
		return AgentEntry{
			.id = "default",
			.name = "Default Agent",
			.active = true,
		};
	}

} // namespace blazeclaw::gateway
