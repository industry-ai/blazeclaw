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

TEST_CASE("Native startup keeps bridge alive when runner init fails", "[agentchat][native][failedtofetch][contract]") {
	const auto viewPath = std::filesystem::path("E:/gitRepo/blazeClaw/blazeclaw/BlazeClawMfc/src/app/BlazeClawAgentChatView.cpp");
	const std::string source = ReadUtf8File(viewPath);

	REQUIRE(source.find("if (!nativeRunner->Initialize())") != std::string::npos);
	REQUIRE(source.find("m_nativeBridgeHost = std::move(bridgeHost);") != std::string::npos);
	REQUIRE(source.find("m_nativeRunner.reset();") != std::string::npos);
	REQUIRE(source.find("m_nativeRunnerStarted = false;") != std::string::npos);
	REQUIRE(source.find("m_nativeModeDegraded = true;") != std::string::npos);
	REQUIRE(source.find("m_nativeRuntimeStarted = true;") != std::string::npos);
	REQUIRE(source.find("Native runtime started in degraded mode (bridge on, runner off)") != std::string::npos);
	REQUIRE(source.find("Native mode running degraded (bridge on, runner off)") != std::string::npos);
}
