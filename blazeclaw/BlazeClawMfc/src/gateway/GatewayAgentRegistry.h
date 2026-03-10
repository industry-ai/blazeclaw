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

	struct AgentFileContentEntry {
		std::string path;
		std::size_t size = 0;
		std::uint64_t updatedMs = 0;
		std::string content;
	};

	struct AgentFileDeleteResult {
		AgentFileContentEntry file;
		bool deleted = false;
	};

	struct AgentFileExistsResult {
		std::string path;
		bool exists = false;
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
		AgentFileContentEntry GetFile(const std::string& requestedId, const std::string& path) const;
		AgentFileContentEntry SetFile(
			const std::string& requestedId,
			const std::string& path,
			const std::string& content);
		AgentFileDeleteResult DeleteFile(const std::string& requestedId, const std::string& path);
		AgentFileExistsResult ExistsFile(const std::string& requestedId, const std::string& path) const;

	private:
		struct FileOverride {
			std::string content;
			std::uint64_t updatedMs = 0;
		};

		static std::string NormalizeAgentId(const std::string& value);
		static AgentEntry BuildDefaultAgent();

		std::unordered_map<std::string, AgentEntry> m_agents;
		std::unordered_map<std::string, FileOverride> m_fileOverrides;
	};

} // namespace blazeclaw::gateway
