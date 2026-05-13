#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::filesystem::path ResolveRepoRoot()
	{
		std::filesystem::path cursor = std::filesystem::current_path();
		for (int depth = 0; depth < 8; ++depth) {
			const auto directCandidate =
				cursor / "BlazeClawMfc" / "src" / "gateway" /
				"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
			const auto nestedCandidate =
				cursor / "blazeclaw" / "BlazeClawMfc" / "src" / "gateway" /
				"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
			if (std::filesystem::exists(directCandidate) ||
				std::filesystem::exists(nestedCandidate)) {
				return cursor;
			}

			if (!cursor.has_parent_path()) {
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor) {
				break;
			}
			cursor = parent;
		}

		return std::filesystem::current_path();
	}

	std::filesystem::path ResolveProjectPath(const std::filesystem::path& relative)
	{
		const auto root = ResolveRepoRoot();
		const auto direct = root / relative;
		if (std::filesystem::exists(direct)) {
			return direct;
		}

		return root / "blazeclaw" / relative;
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"Phase 6 parity: speech lifecycle and segment propagation surfaces remain wired",
	"[parity][phase6][speech][lifecycle]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));

	REQUIRE(speechHandler.find("gateway.speech.lifecycle") != std::string::npos);
	REQUIRE(speechHandler.find("gateway.speech.segment") != std::string::npos);
	REQUIRE(speechHandler.find("supportsSegments") != std::string::npos);
	REQUIRE(speechHandler.find("supportsInterim") != std::string::npos);
	REQUIRE(speechHandler.find("speechSession") != std::string::npos);
	REQUIRE(speechHandler.find("\"segment\"") != std::string::npos);
}

TEST_CASE(
	"Phase 6 parity: transcript handoff and chat surface telemetry remain present",
	"[parity][phase6][speech][handoff]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto chatPipelinePath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string chatPipeline = ReadTextFile(ResolveProjectPath(chatPipelinePath));

	REQUIRE(speechHandler.find("transcriptInjection") != std::string::npos);
	REQUIRE(speechHandler.find("speechArtifact") != std::string::npos);
	REQUIRE(speechHandler.find("gateway.speech.forwarding") != std::string::npos);
	REQUIRE(chatPipeline.find("gateway.chat.orchestration.surface.parity") != std::string::npos);
	REQUIRE(chatPipeline.find("voiceTranscriptInjected") != std::string::npos);
	REQUIRE(chatPipeline.find("transcriptInjectionJson") != std::string::npos);
}

TEST_CASE(
	"Phase 6 parity: optional TTS speech methods and markdown cleanup are registered",
	"[parity][phase6][speech][tts]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto gatewayHostPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.h";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string gatewayHost = ReadTextFile(ResolveProjectPath(gatewayHostPath));

	REQUIRE(speechHandler.find("NormalizeMarkdownToPlainText") != std::string::npos);
	REQUIRE(speechHandler.find("\"speech.speak\"") != std::string::npos);
	REQUIRE(speechHandler.find("\"speech.stop\"") != std::string::npos);
	REQUIRE(speechHandler.find("\"speech.status\"") != std::string::npos);
	REQUIRE(speechHandler.find("gateway.speech.tts.speak") != std::string::npos);
	REQUIRE(speechHandler.find("gateway.speech.tts.stop") != std::string::npos);
	REQUIRE(speechHandler.find("gateway.speech.tts.status") != std::string::npos);
	REQUIRE(gatewayHost.find("SetSpeechSpeakCallback") != std::string::npos);
	REQUIRE(gatewayHost.find("SetSpeechStopCallback") != std::string::npos);
	REQUIRE(gatewayHost.find("SetSpeechStatusCallback") != std::string::npos);
}
