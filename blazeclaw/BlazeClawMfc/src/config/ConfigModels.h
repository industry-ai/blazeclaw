#pragma once

#include "pch.h"

namespace blazeclaw::config {

struct GatewayConfig {
  std::wstring bindAddress = L"127.0.0.1";
  std::uint16_t port = 56789;
};

struct AgentConfig {
  std::wstring model = L"default";
  bool enableStreaming = true;
};

struct AppConfig {
  GatewayConfig gateway;
  AgentConfig agent;
  std::vector<std::wstring> enabledChannels;
};

} // namespace blazeclaw::config
