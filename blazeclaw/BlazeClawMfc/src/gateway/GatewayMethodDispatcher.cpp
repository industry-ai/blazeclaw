#include "pch.h"
#include "GatewayMethodDispatcher.h"

#include <algorithm>

namespace blazeclaw::gateway {

void GatewayMethodDispatcher::Register(std::string method, MethodHandler handler) {
  if (method.empty() || handler == nullptr) {
    return;
  }

  m_handlers.insert_or_assign(std::move(method), std::move(handler));
}

protocol::ResponseFrame GatewayMethodDispatcher::Dispatch(const protocol::RequestFrame& request) const {
  const auto it = m_handlers.find(request.method);
  if (it != m_handlers.end()) {
    return it->second(request);
  }

  return protocol::ResponseFrame{
      .id = request.id,
      .ok = false,
      .payloadJson = std::nullopt,
      .error = protocol::ErrorShape{
          .code = "method_not_implemented",
          .message = "Gateway method is not implemented in current milestone.",
          .detailsJson = "{\"method\":\"" + request.method + "\"}",
          .retryable = false,
          .retryAfterMs = std::nullopt,
      },
  };
}

std::size_t GatewayMethodDispatcher::RegisteredMethodCount() const noexcept {
  return m_handlers.size();
}

std::vector<std::string> GatewayMethodDispatcher::RegisteredMethods() const {
  std::vector<std::string> methods;
  methods.reserve(m_handlers.size());

  for (const auto& [method, _] : m_handlers) {
    methods.push_back(method);
  }

  std::sort(methods.begin(), methods.end());
  return methods;
}

} // namespace blazeclaw::gateway
