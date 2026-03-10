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

	AgentFileContentEntry GatewayAgentRegistry::GetFile(const std::string& requestedId, const std::string& path) const {
		const auto files = ListFiles(requestedId);
		AgentFileEntry chosen = files.empty() ? AgentFileEntry{} : files.front();

		if (!path.empty()) {
			const auto it = std::find_if(files.begin(), files.end(), [&](const AgentFileEntry& file) {
				return file.path == path;
				});

			if (it != files.end()) {
				chosen = *it;
			}
			else {
				chosen.path = path;
				chosen.size = 0;
				chosen.updatedMs = 1735689610000;
			}
		}

		const AgentEntry agent = Get(requestedId);
		const std::string overrideKey = agent.id + "::" + chosen.path;
		const auto overrideIt = m_fileOverrides.find(overrideKey);
		if (overrideIt != m_fileOverrides.end()) {
			return AgentFileContentEntry{
				.path = chosen.path,
				.size = overrideIt->second.content.size(),
				.updatedMs = overrideIt->second.updatedMs,
				.content = overrideIt->second.content,
			};
		}

		return AgentFileContentEntry{
			.path = chosen.path,
			.size = chosen.size,
			.updatedMs = chosen.updatedMs,
			.content = chosen.path.empty() ? "" : "seeded_content_for_" + chosen.path,
		};
	}

	AgentFileContentEntry GatewayAgentRegistry::SetFile(
		const std::string& requestedId,
		const std::string& path,
		const std::string& content) {
		const AgentEntry agent = Get(requestedId);
		const auto files = ListFiles(agent.id);
		const std::string resolvedPath = path.empty()
			? (files.empty() ? ("agents/" + agent.id + "/note.txt") : files.front().path)
			: path;

		const std::string key = agent.id + "::" + resolvedPath;
		const std::uint64_t updatedMs = 1735689620000 + static_cast<std::uint64_t>(m_fileOverrides.size());
		m_fileOverrides.insert_or_assign(key, FileOverride{ .content = content, .updatedMs = updatedMs });

		return AgentFileContentEntry{
			.path = resolvedPath,
			.size = content.size(),
			.updatedMs = updatedMs,
			.content = content,
		};
	}

	AgentFileDeleteResult GatewayAgentRegistry::DeleteFile(const std::string& requestedId, const std::string& path) {
		const AgentEntry agent = Get(requestedId);
		const auto files = ListFiles(agent.id);
		const std::string resolvedPath = path.empty()
			? (files.empty() ? ("agents/" + agent.id + "/note.txt") : files.front().path)
			: path;

		const std::string key = agent.id + "::" + resolvedPath;
		const auto erased = m_fileOverrides.erase(key);
		if (erased > 0) {
			return AgentFileDeleteResult{
				.file = AgentFileContentEntry{
					.path = resolvedPath,
					.size = 0,
					.updatedMs = 1735689630000 + static_cast<std::uint64_t>(m_fileOverrides.size()),
					.content = "",
				},
				.deleted = true,
			};
		}

		return AgentFileDeleteResult{
			.file = GetFile(agent.id, resolvedPath),
			.deleted = false,
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
