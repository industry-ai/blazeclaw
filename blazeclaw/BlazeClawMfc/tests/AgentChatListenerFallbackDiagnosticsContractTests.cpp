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

TEST_CASE("Runtime bridge config exposes listener health and effective mode", "[agentchat][native][listener-fallback][contract]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("m_nativeHttpListenerStarted") != std::string::npos);
	REQUIRE(source.find("m_nativeHttpListenerPort") != std::string::npos);
	REQUIRE(source.find("nativeBridgeEffectiveMode") != std::string::npos);
	REQUIRE(source.find("native-inprocess+http") != std::string::npos);
	REQUIRE(source.find("native-inprocess-only") != std::string::npos);
}

TEST_CASE("Chat API error classification includes effective listener diagnostics", "[agentchat][native][listener-fallback][contract]") {
	const auto apiPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/web/agent-chat-vanilla/html/js/api/chatApi.js");
	const std::string source = ReadUtf8File(apiPath);

	REQUIRE(source.find("nativeBridgeEffectiveMode") != std::string::npos);
	REQUIRE(source.find("nativeHttpListenerStarted") != std::string::npos);
	REQUIRE(source.find("mode=native-inprocess-only") != std::string::npos);
	REQUIRE(source.find("http_listener=disabled") != std::string::npos);
}
