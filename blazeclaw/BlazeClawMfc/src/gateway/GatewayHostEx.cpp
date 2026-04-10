#include "pch.h"
#include "GatewayHostEx.h"

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	GatewayHostEx::GatewayHostEx(const GatewayHost* legacyHost) noexcept
		: m_legacyHost(legacyHost) {}

	protocol::ResponseFrame GatewayHostEx::RouteRequest(
		const protocol::RequestFrame& request) const {
		if (m_legacyHost == nullptr) {
			return protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = protocol::ErrorShape{
					.code = "stage_host_unavailable",
					.message = "GatewayHostEx has no legacy host backing instance.",
					.detailsJson = std::nullopt,
					.retryable = true,
					.retryAfterMs = 0,
				},
			};
		}

		return m_legacyHost->RouteRequestLegacy(request);
	}

	bool GatewayHostEx::IsHealthy() const noexcept {
		return m_legacyHost != nullptr;
	}

} // namespace blazeclaw::gateway
