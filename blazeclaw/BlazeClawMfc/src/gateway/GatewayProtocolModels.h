#pragma once

#include "pch.h"

namespace blazeclaw::gateway::protocol {

struct ErrorShape {
  std::string code;
  std::string message;
  std::optional<std::string> detailsJson;
  std::optional<bool> retryable;
  std::optional<std::uint64_t> retryAfterMs;
};

struct RequestFrame {
  std::string id;
  std::string method;
  std::optional<std::string> paramsJson;
};

struct ResponseFrame {
  std::string id;
  bool ok = false;
  std::optional<std::string> payloadJson;
  std::optional<ErrorShape> error;
};

struct EventFrame {
  std::string eventName;
  std::optional<std::string> payloadJson;
  std::optional<std::uint64_t> seq;
  std::optional<std::uint64_t> stateVersion;
};

} // namespace blazeclaw::gateway::protocol
