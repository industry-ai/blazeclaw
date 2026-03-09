#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway {

	class GatewayMethodDispatcher {
	public:
		using MethodHandler = std::function<protocol::ResponseFrame(const protocol::RequestFrame& request)>;

		void Register(std::string method, MethodHandler handler);
		[[nodiscard]] protocol::ResponseFrame Dispatch(const protocol::RequestFrame& request) const;
		[[nodiscard]] std::size_t RegisteredMethodCount() const noexcept;
		[[nodiscard]] std::vector<std::string> RegisteredMethods() const;

	private:
		std::unordered_map<std::string, MethodHandler> m_handlers;
	};

} // namespace blazeclaw::gateway
