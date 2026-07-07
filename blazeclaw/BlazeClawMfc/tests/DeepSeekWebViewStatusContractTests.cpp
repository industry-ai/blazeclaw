#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::string ReadFileUtf8(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

	std::string ReadSettingsDialogSource()
	{
		return ReadFileUtf8(
			std::filesystem::path("BlazeClawMfc") /
			"src" /
			"app" /
			"SettingsDialog.cpp");
	}

	std::string ReadServiceManagerSource()
	{
		return ReadFileUtf8(
			std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ServiceManager.cpp");
	}

	std::string ReadBridgeSource()
	{
		return ReadFileUtf8(
			std::filesystem::path("BlazeClawMfc") /
			"src" /
			"app" /
			"CBridge.cpp");
	}

	std::string ReadChatEventsSource()
	{
		return ReadFileUtf8(
			std::filesystem::path("BlazeClawMfc") /
			"web" /
			"chat" /
			"chat-events.js");
	}

} // namespace

TEST_CASE(
	"Settings dialog contract: DeepSeek target priority is explicitly represented",
	"[deepseek][settings][contract]")
{
	const std::string source = ReadSettingsDialogSource();
	REQUIRE(source.find("bool IsDeepSeekModelId(const std::string& modelId)") != std::string::npos);
	REQUIRE(source.find("IsDeepSeekModelId(m_models[i].id)") != std::string::npos);
	REQUIRE(source.find("if (i < previousEnabled.size() && !previousEnabled[i]) {") != std::string::npos);
}

TEST_CASE(
	"ServiceManager contract: active provider mutation is applied with auth generation bump",
	"[deepseek][servicemanager][contract]")
{
	const std::string source = ReadServiceManagerSource();
	REQUIRE(source.find("runtime_mutation.auth_generation_bumped") != std::string::npos);
	REQUIRE(source.find("m_activeConfig.chat.activeProvider = ToWide(nextProvider);") != std::string::npos);
	REQUIRE(source.find("m_activeConfig.chat.activeModel = ToWide(nextModel);") != std::string::npos);
	REQUIRE(source.find("runtime_mutation.auth_generation_reject") == std::string::npos);
}

TEST_CASE(
	"Bridge lifecycle contract: DeepSeek readiness metadata emitted in lifecycle payload",
	"[deepseek][bridge][contract]")
{
	const std::string source = ReadBridgeSource();
	REQUIRE(source.find("ParseDeepSeekEnabledModelsFromConfig()") != std::string::npos);
	REQUIRE(source.find("BuildDeepSeekConfiguredModels()") != std::string::npos);
	REQUIRE(source.find("deepSeekCredentialReady != m_lastDeepSeekCredentialReady") != std::string::npos);
	REQUIRE(source.find("deepSeekEnabledModels") != std::string::npos);
	REQUIRE(source.find("deepSeekConfiguredModels") != std::string::npos);
}

TEST_CASE(
	"Web chat status contract: lifecycle status appends DeepSeek readiness details",
	"[deepseek][webview][contract]")
{
	const std::string source = ReadChatEventsSource();
	REQUIRE(source.find("message.deepseek") != std::string::npos);
	REQUIRE(source.find("credential=ready") != std::string::npos);
	REQUIRE(source.find("| deepseek:") != std::string::npos);
}
