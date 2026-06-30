#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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

TEST_CASE("Native WebView bridge emits requestId-correlated observability traces", "[agentchat][native][observability][contract]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("native bridge request received requestId=") != std::string::npos);
	REQUIRE(source.find("native bridge request posted requestId=") != std::string::npos);
	REQUIRE(source.find("native bridge delta requestId=") != std::string::npos);
	REQUIRE(source.find("native bridge final requestId=") != std::string::npos);
	REQUIRE(source.find("native bridge error requestId=") != std::string::npos);
	REQUIRE(source.find("native bridge response sent requestId=") != std::string::npos);
}

TEST_CASE("Agent bridge transport preserves native first-failure reason and uses health preflight", "[agentchat][native][observability][contract]") {
	const auto transportPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/web/agent-chat-vanilla/html/js/api/agentBridgeTransport.js");
	const std::string source = ReadUtf8File(transportPath);

	REQUIRE(source.find("function _traceBridge(") != std::string::npos);
	REQUIRE(source.find("function _preflightNativeBridgeHealth(") != std::string::npos);
	REQUIRE(source.find("kind: 'agent.health'") != std::string::npos);
	REQUIRE(source.find("Native bridge request failed (${reason})") != std::string::npos);
	REQUIRE(source.find("fallbackAllowedByPolicy") != std::string::npos);
}
