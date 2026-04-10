#pragma once

#include "IGatewayHostRuntime.h"

namespace blazeclaw::gateway {

	class GatewayHost;

	class GatewayHostEx final : public IGatewayHostRuntime {
	public:
		explicit GatewayHostEx(const GatewayHost* legacyHost) noexcept;

		[[nodiscard]] protocol::ResponseFrame RouteRequest(
			const protocol::RequestFrame& request) const override;

		[[nodiscard]] bool IsHealthy() const noexcept;

	private:
		const GatewayHost* m_legacyHost = nullptr;
	};

} // namespace blazeclaw::gateway
