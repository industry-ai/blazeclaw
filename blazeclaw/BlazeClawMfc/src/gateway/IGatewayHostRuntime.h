#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	class IGatewayHostRuntime {
	public:
		virtual ~IGatewayHostRuntime() = default;

		[[nodiscard]] virtual bool IsHealthy() const noexcept = 0;

		[[nodiscard]] virtual protocol::ResponseFrame RouteRequest(
			const protocol::RequestFrame& request) const = 0;
	};

} // namespace blazeclaw::gateway
