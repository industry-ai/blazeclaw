#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {
	std::string ReadUtf8File(const std::filesystem::path& path) {
		std::ifstream in(path, std::ios::in | std::ios::binary);
		REQUIRE(in.is_open());
		std::ostringstream buffer;
		buffer << in.rdbuf();
		return buffer.str();
	}
}

TEST_CASE("WebView native bridge request-stream-final wiring contract is present", "[agentchat][native][webview][smoke]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("channel != \"agentchat.bridge.request\"") != std::string::npos);
	REQUIRE(source.find("if (kind != \"agent.turn\")") != std::string::npos);
	REQUIRE(source.find("HandleInProcessAgentTurn(requestBody)") != std::string::npos);
	REQUIRE(source.find("agentchat.bridge.stream.delta") != std::string::npos);
	REQUIRE(source.find("agentchat.bridge.stream.final") != std::string::npos);
	REQUIRE(source.find("agentchat.bridge.response") != std::string::npos);

	const auto deltaPos = source.find("agentchat.bridge.stream.delta");
	const auto finalPos = source.find("agentchat.bridge.stream.final");
	REQUIRE(deltaPos != std::string::npos);
	REQUIRE(finalPos != std::string::npos);
	REQUIRE(deltaPos < finalPos);
}

TEST_CASE("Frontend dual transport consumes native bridge stream delta and final events", "[agentchat][native][webview][smoke]") {
	const auto transportPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/web/agent-chat-vanilla/html/js/api/agentBridgeTransport.js");
	const std::string source = ReadUtf8File(transportPath);

	REQUIRE(source.find("channel: 'agentchat.bridge.request'") != std::string::npos);
	REQUIRE(source.find("kind: 'agent.turn'") != std::string::npos);
	REQUIRE(source.find("channel === 'agentchat.bridge.stream.delta'") != std::string::npos);
	REQUIRE(source.find("channel === 'agentchat.bridge.stream.final'") != std::string::npos);
	REQUIRE(source.find("channel === 'agentchat.bridge.response'") != std::string::npos);
	REQUIRE(source.find("if (preferNative && _supportsNativeWebViewBridge())") != std::string::npos);
}
