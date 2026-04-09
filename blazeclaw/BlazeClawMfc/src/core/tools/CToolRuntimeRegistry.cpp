#include "pch.h"
#include "CToolRuntimeRegistry.h"

namespace blazeclaw::core {

	void CToolRuntimeRegistry::RegisterAll(
		blazeclaw::gateway::GatewayHost& host,
		const Dependencies& deps) const
	{
		if (deps.registerImapSmtp)
		{
			deps.registerImapSmtp(host);
		}
		if (deps.registerContentPolishing)
		{
			deps.registerContentPolishing(host);
		}
		if (deps.registerBraveSearch)
		{
			deps.registerBraveSearch(host);
		}
		if (deps.registerBaiduSearch)
		{
			deps.registerBaiduSearch(host);
		}
	}

} // namespace blazeclaw::core
