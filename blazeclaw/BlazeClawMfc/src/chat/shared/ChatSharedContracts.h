#pragma once

#include "../../gateway/GatewayProtocolModels.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace blazeclaw::chat::shared {

	class IChatRuntimeLifecycle {
	public:
		virtual ~IChatRuntimeLifecycle() = default;

		[[nodiscard]] virtual bool Start() = 0;
		virtual void Stop() = 0;
		[[nodiscard]] virtual bool IsHealthy() const = 0;
	};

	class IChatRequestOrchestrator {
	public:
		virtual ~IChatRequestOrchestrator() = default;

		[[nodiscard]] virtual blazeclaw::gateway::protocol::ResponseFrame Route(
			const blazeclaw::gateway::protocol::RequestFrame& request) const = 0;
	};

	class IChatStreamEventNormalizer {
	public:
		virtual ~IChatStreamEventNormalizer() = default;

		[[nodiscard]] virtual nlohmann::json Normalize(
			const nlohmann::json& eventPayload) const = 0;
	};

	class IChatSessionStateStore {
	public:
		virtual ~IChatSessionStateStore() = default;

		virtual void BeginRequest(const std::string& requestId) = 0;
		virtual void CancelRequest(const std::string& requestId) = 0;
		virtual void CompleteRequest(const std::string& requestId) = 0;

		[[nodiscard]] virtual bool IsRequestActive(const std::string& requestId) const = 0;
		[[nodiscard]] virtual bool IsRequestCancelled(const std::string& requestId) const = 0;
	};

	class IChatTelemetryHooks {
	public:
		virtual ~IChatTelemetryHooks() = default;

		virtual void OnCounter(
			const std::string& name,
			const std::uint64_t value) = 0;
		virtual void OnEvent(
			const std::string& name,
			const nlohmann::json& payload) = 0;
	};

} // namespace blazeclaw::chat::shared
