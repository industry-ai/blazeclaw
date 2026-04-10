#pragma once

#include "IGatewayHostRuntime.h"

namespace blazeclaw::gateway {

	class GatewayHost;
	class ChatRunPipelineOrchestrator;

	struct GatewayHostExDependencies {
		const GatewayHost* legacyHost = nullptr;
		const ChatRunPipelineOrchestrator* stagePipeline = nullptr;
	};

	class GatewayHostEx final : public IGatewayHostRuntime {
	public:
		explicit GatewayHostEx(const GatewayHostExDependencies& dependencies) noexcept;

		[[nodiscard]] protocol::ResponseFrame RouteRequest(
			const protocol::RequestFrame& request) const override;

		[[nodiscard]] bool IsHealthy() const noexcept;

	private:
		GatewayHostExDependencies m_dependencies;
	};

} // namespace blazeclaw::gateway
