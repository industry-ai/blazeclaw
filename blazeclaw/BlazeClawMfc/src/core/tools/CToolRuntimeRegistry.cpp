#include "pch.h"
#include "CToolRuntimeRegistry.h"

namespace blazeclaw::core {

	void CToolRuntimeRegistry::RegisterAll(
		blazeclaw::gateway::GatewayHost& host,
		const ToolRuntimePolicySettings& toolPolicy,
		const Dependencies& deps) const
	{
		if (deps.registerImapSmtp)
		{
			deps.registerImapSmtp(host, toolPolicy);
		}
		if (deps.registerContentPolishing)
		{
			deps.registerContentPolishing(host);
		}
		if (deps.registerBraveSearch)
		{
			deps.registerBraveSearch(host, toolPolicy);
		}
		if (deps.registerBaiduSearch)
		{
			deps.registerBaiduSearch(host, toolPolicy);
		}
	}

	void CToolRuntimeRegistry::RegisterWithAdapters(
		blazeclaw::gateway::GatewayHost& host,
		const ToolRuntimePolicySettings& toolPolicy,
		const std::vector<ToolCapabilityAdapter*>& adapters,
		const Dependencies& deps) const
	{
		const extensions::RuntimeToolPolicySnapshot adapterPolicy{
			.imapSmtpSkillRoot = toolPolicy.imapSmtpSkillRoot,
			.baiduSearchSkillRoot = toolPolicy.baiduSearchSkillRoot,
			.braveSearchSkillRoot = toolPolicy.braveSearchSkillRoot,
			.openClawWebBrowsingSkillRoot = toolPolicy.openClawWebBrowsingSkillRoot,
			.braveRequireApiKey = toolPolicy.braveRequireApiKey,
			.braveApiKeyPresent = toolPolicy.braveApiKeyPresent,
			.enableOpenClawWebBrowsingFallback =
				toolPolicy.enableOpenClawWebBrowsingFallback,
		};

		for (const auto* adapter : adapters)
		{
			if (adapter == nullptr)
			{
				continue;
			}

			adapter->RegisterRuntimeTools(
				extensions::RuntimeToolAdapterContext{
					.host = host,
				   .toolPolicy = adapterPolicy,
				});
		}

		RegisterAll(host, toolPolicy, deps);
	}

} // namespace blazeclaw::core
