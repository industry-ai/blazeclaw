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

} // namespace blazeclaw::core
