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

TEST_CASE("WebView message intake persists pending payload before WM dispatch", "[agentchat][native][webview][intake][contract]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("TryGetWebMessageAsString") != std::string::npos);
	REQUIRE(source.find("get_WebMessageAsJson") != std::string::npos);
	REQUIRE(source.find("m_pendingWebMessageJson = message;") != std::string::npos);
	REQUIRE(source.find("PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED);") != std::string::npos);

	const auto pendingAssignPos = source.find("m_pendingWebMessageJson = message;");
	const auto postMessagePos = source.find("PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED);");
	REQUIRE(pendingAssignPos != std::string::npos);
	REQUIRE(postMessagePos != std::string::npos);
	REQUIRE(pendingAssignPos < postMessagePos);
}

TEST_CASE("WebView intake dispatch contract keeps health and turn handlers reachable", "[agentchat][native][webview][intake][contract]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("if (kind == \"agent.health\")") != std::string::npos);
	REQUIRE(source.find("if (kind != \"agent.turn\")") != std::string::npos);
	REQUIRE(source.find("native bridge request received requestId=%s kind=%s") != std::string::npos);
	REQUIRE(source.find("native bridge web message dropped (empty pending payload)") != std::string::npos);
}
