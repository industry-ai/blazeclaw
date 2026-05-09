#pragma once

#include "IGatewayHostRuntime.h"

#include <functional>

namespace blazeclaw::gateway {

	class ChatRunPipelineOrchestrator;

	struct GatewayHostExDependencies {
		std::function<protocol::ResponseFrame(const protocol::RequestFrame&)> routeLegacyRequest;
		std::function<bool()> isLegacyHealthy;
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
