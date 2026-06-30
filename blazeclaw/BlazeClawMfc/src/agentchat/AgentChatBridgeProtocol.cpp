#include "pch.h"
#include "AgentChatBridgeProtocol.h"

namespace blazeclaw::agentchat {

	bool AgentChatBridgeProtocol::IsAgentPath(
		const std::string& path,
		const bool allowOpenClawAliases) {
		if (path == kBlazeClawAgentPath) {
			return true;
		}
		return allowOpenClawAliases && path == kOpenClawAgentPath;
	}

	bool AgentChatBridgeProtocol::IsPushPath(
		const std::string& path,
		const bool allowOpenClawAliases) {
		if (path == kBlazeClawAgentPushPath) {
			return true;
		}
		return allowOpenClawAliases && path == kOpenClawAgentPushPath;
	}

	bool AgentChatBridgeProtocol::IsCollaborationPath(
		const std::string& path) {
		return path == kCollaborationPrefix ||
			(path.size() > std::char_traits<char>::length(kCollaborationPrefix) &&
				path.rfind(kCollaborationPrefix, 0) == 0);
	}

	bool AgentChatBridgeProtocol::IsTtsSynthesizePath(
		const std::string& path) {
		return path == kTtsSynthesizePath;
	}

	AgentChatBridgeHttpResponse AgentChatBridgeProtocol::BuildHealthResponse(
		const std::string& gatewayUrl,
		const bool running) {
		AgentChatBridgeHttpResponse response;
		response.statusCode = 200;
		response.body =
			"{\"ok\":true,\"service\":\"blazeclaw-agentchat-native-bridge\",\"running\":" +
			std::string(running ? "true" : "false") +
			",\"gateway\":\"" + gatewayUrl + "\"}";
		return response;
	}

	AgentChatBridgeHttpResponse AgentChatBridgeProtocol::BuildNotFoundResponse() {
		AgentChatBridgeHttpResponse response;
		response.statusCode = 404;
		response.body = "{\"ok\":false,\"error\":\"not_found\"}";
		return response;
	}

} // namespace blazeclaw::agentchat
