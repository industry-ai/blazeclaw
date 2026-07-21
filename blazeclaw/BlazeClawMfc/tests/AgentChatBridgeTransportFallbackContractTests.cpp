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

TEST_CASE("Agent bridge transport performs health preflight in bridge-risk scenarios", "[agentchat][native][failedtofetch][contract]") {
	const auto transportPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/web/agent-chat-vanilla/html/js/api/agentBridgeTransport.js");
	const std::string source = ReadUtf8File(transportPath);

	REQUIRE(source.find("async function _preflightHttpBridgeHealth") != std::string::npos);
	REQUIRE(source.find("const url = `${baseUrl}/health`;") != std::string::npos);
	REQUIRE(source.find("const shouldPreflightHttp =") != std::string::npos);
	REQUIRE(source.find("reachabilityHint === 'bridge-unavailable'") != std::string::npos);
	REQUIRE(source.find("throw new Error(`Agent bridge unavailable (${reason})`)" ) != std::string::npos);
}

TEST_CASE("Chat API normalizes generic fetch failure into actionable bridge error", "[agentchat][native][failedtofetch][contract]") {
	const auto chatApiPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/web/agent-chat-vanilla/html/js/api/chatApi.js");
	const std::string source = ReadUtf8File(chatApiPath);

	REQUIRE(source.find("/Failed to fetch/i.test(rawMessage)") != std::string::npos);
	REQUIRE(source.find("Agent bridge unavailable (fetch_failed; mode=") != std::string::npos);
}
