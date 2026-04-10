#include "pch.h"
#include "GatewayHostEx.h"

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	GatewayHostEx::GatewayHostEx(const GatewayHostExDependencies& dependencies) noexcept
		: m_dependencies(dependencies) {}

	protocol::ResponseFrame GatewayHostEx::RouteRequest(
		const protocol::RequestFrame& request) const {
		if (m_dependencies.legacyHost == nullptr) {
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

		return m_dependencies.legacyHost->RouteRequestLegacy(request);
	}

	bool GatewayHostEx::IsHealthy() const noexcept {
		return m_dependencies.legacyHost != nullptr &&
			m_dependencies.legacyHost->IsHealthy();
	}

} // namespace blazeclaw::gateway
