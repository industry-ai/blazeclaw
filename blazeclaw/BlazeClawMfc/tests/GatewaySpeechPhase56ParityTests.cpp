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

TEST_CASE(
	"Sherpa Step 10 diagnostics retain bounded contracts and gate verbose traces",
	"[speech][sherpa][step10]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto sherpaEnginePath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"runtime" /
		"SpeechRecognition" /
		"engines" /
		"SherpaZipformerStreamingEngine.cpp";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string sherpaEngine = ReadTextFile(ResolveProjectPath(sherpaEnginePath));

	REQUIRE(speechHandler.find("gateway.speech.debug.snapshot") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaFinalOutcome") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaEncoderStateCacheBindingCount") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaDecoderJoinerContractFailureCount") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaContractJoinerOutputShape") != std::string::npos);

	REQUIRE(sherpaEngine.find("BLAZECLAW_SHERPA_VERBOSE_TRACE") != std::string::npos);
	REQUIRE(sherpaEngine.find("IsSherpaVerboseTraceEnabled() && streamState.chunkCount") != std::string::npos);
	REQUIRE(sherpaEngine.find("IsSherpaVerboseTraceEnabled() && tokenId != m_blankId") != std::string::npos);
}

TEST_CASE(
	"Sherpa first-token diagnostics expose provider and CUDA fallback contracts",
	"[speech][sherpa][first-token][diagnostics]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto appPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"app" /
		"BlazeClawMfcApp.cpp";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string app = ReadTextFile(ResolveProjectPath(appPath));

	REQUIRE(speechHandler.find("buildSpeechRuntimeProviderJson") != std::string::npos);
	REQUIRE(speechHandler.find("effectiveExecutionProvider") != std::string::npos);
	REQUIRE(speechHandler.find("cudaExecutionProviderAvailable") != std::string::npos);
	REQUIRE(speechHandler.find("cudaExecutionProviderEnabled") != std::string::npos);
	REQUIRE(speechHandler.find("cudaExecutionProviderReason") != std::string::npos);
	REQUIRE(speechHandler.find("streamingLatencyProfile") != std::string::npos);
	REQUIRE(speechHandler.find("streamingPreviewChunkMs") != std::string::npos);
	REQUIRE(speechHandler.find("streamingPreviewLookbackMs") != std::string::npos);
	REQUIRE(speechHandler.find("threads") != std::string::npos);
	REQUIRE(speechHandler.find("executionMode") != std::string::npos);

	REQUIRE(app.find("startup.runtime") != std::string::npos);
	REQUIRE(app.find("startup.runtime.cuda") != std::string::npos);
	REQUIRE(app.find("effectiveProvider=") != std::string::npos);
	REQUIRE(app.find("available=") != std::string::npos);
	REQUIRE(app.find("enabled=") != std::string::npos);
	REQUIRE(app.find("reason=") != std::string::npos);
}

TEST_CASE(
	"Sherpa first-token diagnostics retain native, gateway, and WebView timing fields",
	"[speech][sherpa][first-token][timing]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto webIndexPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"index.js";
	const auto webControllerPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-controller.js";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string webIndex = ReadTextFile(ResolveProjectPath(webIndexPath));
	const std::string webController = ReadTextFile(ResolveProjectPath(webControllerPath));

	REQUIRE(speechHandler.find("buildFirstTokenTimingJson") != std::string::npos);
	REQUIRE(speechHandler.find("firstAudioAcceptedOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenEncoderStartOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenEncoderEndOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenDecoderStartOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenJoinerStartOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenPartialTextOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("firstTokenNativePayloadReadyOffsetMs") != std::string::npos);
	REQUIRE(speechHandler.find("gatewayNativePayloadReadyOffsetMs") != std::string::npos);

	REQUIRE(webController.find("firstTokenTiming") != std::string::npos);
	REQUIRE(webIndex.find("emitFirstTokenRenderDiagnostic") != std::string::npos);
	REQUIRE(webIndex.find("clickToRenderMs") != std::string::npos);
	REQUIRE(webIndex.find("previewResponseToRenderMs") != std::string::npos);
	REQUIRE(webIndex.find("speech-preview-diagnostic") != std::string::npos);
}

TEST_CASE(
	"Sherpa empty-final transcript path maps to explicit native failure semantics",
	"[speech][sherpa][final][empty-transcript]")
{
	const auto speechHandlerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.SpeechRecognition.cpp";
	const auto coordinatorPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"SpeechTranscriptionCoordinator.cpp";
	const auto sherpaEnginePath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"runtime" /
		"SpeechRecognition" /
		"engines" /
		"SherpaZipformerStreamingEngine.cpp";

	const std::string speechHandler = ReadTextFile(ResolveProjectPath(speechHandlerPath));
	const std::string coordinator = ReadTextFile(ResolveProjectPath(coordinatorPath));
	const std::string sherpaEngine = ReadTextFile(ResolveProjectPath(sherpaEnginePath));

	REQUIRE(sherpaEngine.find("const bool missingFinalTranscript") != std::string::npos);
	REQUIRE(sherpaEngine.find("const bool isWarmupRequest") != std::string::npos);
	REQUIRE(sherpaEngine.find("speech-warmup-") != std::string::npos);
	REQUIRE(sherpaEngine.find("final transcript unavailable:") != std::string::npos);
	REQUIRE(sherpaEngine.find("SpeechRecognitionErrorCode::InferenceFailed") != std::string::npos);

	REQUIRE(speechHandler.find("completedWithoutTranscript") != std::string::npos);
	REQUIRE(speechHandler.find("!livePreviewOnly") != std::string::npos);
	REQUIRE(speechHandler.find("transcribe.sessionState.stage =") != std::string::npos);
	REQUIRE(speechHandler.find("SpeechSessionStage::Failed") != std::string::npos);
	REQUIRE(speechHandler.find("inference_failed") != std::string::npos);
	REQUIRE(speechHandler.find("no_speech_detected") != std::string::npos);
	REQUIRE(speechHandler.find("noSpeechOutcome") != std::string::npos);
	REQUIRE(speechHandler.find("microphone level/input channel") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaInputHealthIndex") != std::string::npos);
	REQUIRE(speechHandler.find("sherpaChunkEnergyAvgPermille") != std::string::npos);
	REQUIRE(speechHandler.find("captureChannelIndex") != std::string::npos);

	REQUIRE(coordinator.find("!result.ok && result.sessionState.stage == SpeechSessionStage::Completed") != std::string::npos);
	REQUIRE(coordinator.find("return SpeechExecutionStage::Failed") != std::string::npos);
	REQUIRE(sherpaEngine.find("[SherpaStreaming][capture.quality]") != std::string::npos);
	REQUIRE(sherpaEngine.find("streamState.chunkEnergySum") != std::string::npos);
	REQUIRE(sherpaEngine.find("sherpaInputHealthIndex") != std::string::npos);
	REQUIRE(sherpaEngine.find("streamingInput.source.captureChannelIndex") != std::string::npos);

	const auto recorderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"app" /
		"VoiceRecorder.cpp";
	const std::string recorder = ReadTextFile(ResolveProjectPath(recorderPath));
	REQUIRE(recorder.find("ResolveCaptureChannelIndex") != std::string::npos);
	REQUIRE(recorder.find("adaptiveRingCaptureChannelEnabled") != std::string::npos);
	REQUIRE(recorder.find("ringCaptureChannelFixedOverride") != std::string::npos);
	REQUIRE(recorder.find("captureChannelEnergyPermille") != std::string::npos);
}
