#pragma once

#include <string>

namespace blazeclaw::agentchat {

struct AgentChatBridgeHttpResponse {
	int statusCode = 200;
	std::string contentType = "application/json; charset=utf-8";
	std::string body;
};

class AgentChatBridgeProtocol {
public:
	static constexpr const char* kHealthPath = "/health";
	static constexpr const char* kBlazeClawAgentPath = "/api/blazeclaw-agent";
	static constexpr const char* kBlazeClawAgentPushPath = "/api/blazeclaw-agent-push";
	static constexpr const char* kOpenClawAgentPath = "/api/openclaw-agent";
	static constexpr const char* kOpenClawAgentPushPath = "/api/openclaw-agent-push";
	static constexpr const char* kCollaborationPrefix = "/api/collaboration";
	static constexpr const char* kTtsSynthesizePath = "/api/tts/synthesize";

	static bool IsAgentPath(
		const std::string& path,
		bool allowOpenClawAliases);
	static bool IsPushPath(
		const std::string& path,
		bool allowOpenClawAliases);
	static bool IsCollaborationPath(
		const std::string& path);
	static bool IsTtsSynthesizePath(
		const std::string& path);

	static AgentChatBridgeHttpResponse BuildHealthResponse(
		const std::string& gatewayUrl,
		bool running);
	static AgentChatBridgeHttpResponse BuildNotFoundResponse();
};

} // namespace blazeclaw::agentchat
