#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	class GatewayRequestPolicyGuard {
	public:
		struct Context {
			bool dispatchInitialized = false;
			bool hostRunning = false;
		};

		[[nodiscard]] std::optional<protocol::ErrorShape> Evaluate(
			const protocol::RequestFrame& request,
			const Context& context) const;
	};

} // namespace blazeclaw::gateway
