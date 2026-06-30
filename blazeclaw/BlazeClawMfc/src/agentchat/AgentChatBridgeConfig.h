#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace blazeclaw::agentchat {

struct AgentChatBridgeConfig {
	bool enabled = false;
	std::string mode = "native";
	bool enableHttpListener = true;
	bool enableHttpPushIngress = true;
	bool enableGatewayRouting = true;
	bool enablePushTransport = true;
	bool enableUiInProcessAgentPath = true;
	bool allowNonLoopbackHttpBind = false;
	bool compatibilityOpenClawAliases = true;
	std::string bindAddress = "127.0.0.1";
	std::uint16_t port = 8788;
	std::filesystem::path stateRoot;

	static AgentChatBridgeConfig ResolveFromEnvironment();
};

} // namespace blazeclaw::agentchat
