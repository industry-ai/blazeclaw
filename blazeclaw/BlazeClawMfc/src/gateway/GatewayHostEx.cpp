#include "pch.h"
#include "GatewayHostEx.h"

namespace blazeclaw::gateway {

	GatewayHostEx::GatewayHostEx(const GatewayHostExDependencies& dependencies) noexcept
		: m_dependencies(dependencies) {}

	protocol::ResponseFrame GatewayHostEx::RouteRequest(
		const protocol::RequestFrame& request) const {
		if (!m_dependencies.routeLegacyRequest) {
			return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = "stage_host_unavailable",
					.message = "GatewayHostEx has no legacy host backing instance.",
					.detailsJson = std::nullopt,
					.retryable = true,
					.retryAfterMs = 0,
				});
		}

		return m_dependencies.routeLegacyRequest(request);
	}

	bool GatewayHostEx::IsHealthy() const noexcept {
		return m_dependencies.isLegacyHealthy &&
			m_dependencies.isLegacyHealthy();
	}

} // namespace blazeclaw::gateway
