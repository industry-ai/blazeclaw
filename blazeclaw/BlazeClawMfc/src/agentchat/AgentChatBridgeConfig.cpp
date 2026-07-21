#include "pch.h"
#include "AgentChatBridgeConfig.h"

#include <algorithm>
#include <cstdlib>

namespace blazeclaw::agentchat {
	namespace {
		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string TrimCopy(const std::string& value) {
			const auto begin = std::find_if_not(
				value.begin(),
				value.end(),
				[](unsigned char ch) { return std::isspace(ch) != 0; });
			const auto end = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
			if (begin >= end) {
				return {};
			}
			return std::string(begin, end);
		}

		std::string GetEnv(const char* name) {
			if (name == nullptr || *name == '\0') {
				return {};
			}
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
				return {};
			}
			std::string result(value);
			free(value);
			return TrimCopy(result);
		}

		bool ParseBool(const std::string& value, bool fallback) {
			if (value.empty()) {
				return fallback;
			}
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
				return true;
			}
			if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
				return false;
			}
			return fallback;
		}

		std::uint16_t ParsePort(const std::string& value, std::uint16_t fallback) {
			if (value.empty()) {
				return fallback;
			}
			try {
				const int parsed = std::stoi(value);
				if (parsed <= 0 || parsed > 65535) {
					return fallback;
				}
				return static_cast<std::uint16_t>(parsed);
			}
			catch (...) {
				return fallback;
			}
		}
	}

	AgentChatBridgeConfig AgentChatBridgeConfig::ResolveFromEnvironment() {
		AgentChatBridgeConfig config;
		const std::string modeRaw = GetEnv("BLAZECLAW_AGENTCHAT_BRIDGE_MODE");
		std::string mode = ToLowerCopy(modeRaw);
		if (mode != "native" && mode != "legacy" && mode != "auto") {
			mode = "native";
		}
		config.mode = mode;
		config.enabled = ParseBool(GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_BRIDGE"), false);
		if (config.mode == "native") {
			config.enabled = true;
		}
		else if (config.mode == "legacy") {
			config.enabled = false;
		}
		config.enableHttpListener = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_HTTP_LISTENER"),
			true);
		config.enableHttpPushIngress = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_HTTP_PUSH_INGRESS"),
			true);
		config.enableGatewayRouting = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_GATEWAY_ROUTING"),
			true);
		config.enablePushTransport = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_PUSH_TRANSPORT"),
			true);
		config.enableUiInProcessAgentPath = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_NATIVE_UI_INPROCESS_AGENT_PATH"),
			true);
		config.allowNonLoopbackHttpBind = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_ALLOW_NON_LOOPBACK_HTTP_BIND"),
			false);
		config.compatibilityOpenClawAliases = ParseBool(
			GetEnv("BLAZECLAW_AGENTCHAT_OPENCLAW_ALIASES"),
			true);
		config.bindAddress = GetEnv("BLAZECLAW_AGENT_BRIDGE_BIND_ADDRESS");
		if (config.bindAddress.empty()) {
			config.bindAddress = "127.0.0.1";
		}
		config.port = ParsePort(
			GetEnv("BLAZECLAW_AGENT_BRIDGE_PORT"),
			8788);
		const std::string stateRoot = GetEnv("BLAZECLAW_AGENTCHAT_STATE_DIR");
		if (!stateRoot.empty()) {
			config.stateRoot = std::filesystem::path(stateRoot);
		}
		return config;
	}

} // namespace blazeclaw::agentchat
