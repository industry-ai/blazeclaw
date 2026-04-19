#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"

namespace blazeclaw::gateway {

	void GatewayHost::RegisterRuntimeHandlers() {
		handlers::runtime::RuntimeSurfaceHandlers::RegisterAll(*this);
		handlers::runtime::ChatPipelineHandlers::RegisterAll(*this);
		handlers::runtime::RuntimeOrchestrationStreamingHandlers::RegisterAll(*this);
	}

} // namespace blazeclaw::gateway
