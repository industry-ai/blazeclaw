#pragma once

#include "../../gateway/GatewayHost.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace blazeclaw::core {

	class CToolRuntimeRegistry {
	public:
		struct ToolRuntimePolicySettings {
			std::optional<std::filesystem::path> imapSmtpSkillRoot;
			std::optional<std::filesystem::path> baiduSearchSkillRoot;
			std::optional<std::filesystem::path> braveSearchSkillRoot;
			std::optional<std::filesystem::path> openClawWebBrowsingSkillRoot;
			bool braveRequireApiKey = false;
			bool braveApiKeyPresent = false;
			bool enableOpenClawWebBrowsingFallback = false;
		};

		struct Dependencies {
			std::function<void(
				blazeclaw::gateway::GatewayHost&,
				const ToolRuntimePolicySettings&)> registerImapSmtp;
			std::function<void(blazeclaw::gateway::GatewayHost&)> registerContentPolishing;
			std::function<void(
				blazeclaw::gateway::GatewayHost&,
				const ToolRuntimePolicySettings&)> registerBraveSearch;
			std::function<void(
				blazeclaw::gateway::GatewayHost&,
				const ToolRuntimePolicySettings&)> registerBaiduSearch;
		};

		void RegisterAll(
			blazeclaw::gateway::GatewayHost& host,
			const ToolRuntimePolicySettings& toolPolicy,
			const Dependencies& deps) const;
	};

} // namespace blazeclaw::core
