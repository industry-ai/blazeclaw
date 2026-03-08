#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	class GatewayMethodDispatcher {
	public:
		using MethodHandler = std::function<protocol::ResponseFrame(const protocol::RequestFrame& request)>;

		void Register(std::string method, MethodHandler handler);
		[[nodiscard]] protocol::ResponseFrame Dispatch(const protocol::RequestFrame& request) const;
		[[nodiscard]] std::size_t RegisteredMethodCount() const noexcept;

	private:
		std::unordered_map<std::string, MethodHandler> m_handlers;
	};

} // namespace blazeclaw::gateway
