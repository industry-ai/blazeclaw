#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::gateway {
	class GatewayHost;
}

namespace blazeclaw::core {
	struct SkillsCommandSpec;
	struct SkillsCommandSnapshot;
}

namespace blazeclaw::core::extensions {

	struct RuntimeToolPolicySnapshot {
		std::optional<std::filesystem::path> imapSmtpSkillRoot;
		std::optional<std::filesystem::path> baiduSearchSkillRoot;
		std::optional<std::filesystem::path> braveSearchSkillRoot;
		std::optional<std::filesystem::path> openClawWebBrowsingSkillRoot;
		bool braveRequireApiKey = false;
		bool braveApiKeyPresent = false;
		bool enableOpenClawWebBrowsingFallback = false;
	};

	struct RuntimeCapabilityDescriptor {
		std::string capabilityId;
		std::string owner;
		std::string version = "v1";
		std::string source = "internal";
	};

	struct RuntimeToolAdapterContext {
		blazeclaw::gateway::GatewayHost& host;
		const RuntimeToolPolicySnapshot& toolPolicy;
	};

	struct RuntimeSkillAdapterContext {
		const blazeclaw::core::SkillsCommandSnapshot& commandSnapshot;
		std::vector<blazeclaw::core::SkillsCommandSpec>& mutableCommands;
	};

	class IRuntimeToolCapabilityAdapter {
	public:
		virtual ~IRuntimeToolCapabilityAdapter() = default;

		[[nodiscard]] virtual RuntimeCapabilityDescriptor Describe() const = 0;
		virtual void RegisterRuntimeTools(const RuntimeToolAdapterContext& context) const = 0;
	};

	class IRuntimeSkillCapabilityAdapter {
	public:
		virtual ~IRuntimeSkillCapabilityAdapter() = default;

		[[nodiscard]] virtual RuntimeCapabilityDescriptor Describe() const = 0;
		virtual void EnrichSkillsCommandSnapshot(
			const RuntimeSkillAdapterContext& context) const = 0;
	};

} // namespace blazeclaw::core::extensions
