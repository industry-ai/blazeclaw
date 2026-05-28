#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <ctime>

namespace {

	std::filesystem::path ResolveRepoRoot()
	{
		std::filesystem::path cursor = std::filesystem::current_path();
		for (int depth = 0; depth < 8; ++depth) {
			const auto directCandidate =
				cursor / "BlazeClawMfc" / "tools" / "compare_sherpa_baseline.py";
			const auto nestedCandidate =
				cursor / "blazeclaw" / "BlazeClawMfc" / "tools" / "compare_sherpa_baseline.py";
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

	std::filesystem::path CreateUniqueTempDirectory()
	{
		const int localMarker = 0;
		const auto root = std::filesystem::temp_directory_path() /
			std::filesystem::path("blazeclaw-sherpa-step8-" +
				std::to_string(std::time(nullptr)) + "-" +
				std::to_string(reinterpret_cast<std::uintptr_t>(&localMarker)));
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteTextFile(const std::filesystem::path& path, const std::string& text)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << text;
	}

	int RunCommand(const std::string& command)
	{
		return std::system(command.c_str());
	}

	std::string QuotePath(const std::filesystem::path& path)
	{
		return "\"" + path.string() + "\"";
	}

} // namespace

TEST_CASE(
	"Sherpa Step 8 checker accepts native non-blank final transcript baselines",
	"[speech][sherpa][step8]")
{
	const auto tempRoot = CreateUniqueTempDirectory();
	const auto baselinePath = tempRoot / "step8-pass.sherpa-baseline.json";
	const auto debugLogPath = tempRoot / "debug-log.txt";
	const auto outputPath = tempRoot / "comparison.json";

	WriteTextFile(
		baselinePath,
		"{\n"
		"  \"expectedText\": \"请讲一个笑话\",\n"
		"  \"decodedText\": \"请讲一个笑话\",\n"
		"  \"sampleRate\": 16000,\n"
		"  \"fbankFrameCount\": 176,\n"
		"  \"encoderFrameCount\": 176,\n"
		"  \"joinerCallCount\": 200,\n"
		"  \"blankTokenCount\": 173,\n"
		"  \"decodedTokenCount\": 27,\n"
		"  \"finalOutcome\": \"final_transcript\",\n"
		"  \"hasSegment\": true,\n"
		"  \"fallbackUsed\": false,\n"
		"  \"tokenIds\": \"601 499\",\n"
		"  \"tokenPieces\": \"请 讲\"\n"
		"}\n");

	WriteTextFile(
		debugLogPath,
		"[Telemetry] {\"type\":\"event\",\"event\":\"gateway.telemetry\","
		"\"payload\":{\"event\":\"gateway.speech.lifecycle\","
		"\"payload\":{\"hasSegment\":true}}}\n");

	const auto scriptPath = ResolveProjectPath(
		std::filesystem::path("BlazeClawMfc") / "tools" / "compare_sherpa_baseline.py");
	REQUIRE(std::filesystem::exists(scriptPath));

	const std::string command =
		"python " + QuotePath(scriptPath) +
		" --baseline " + QuotePath(baselinePath) +
		" --debug-log " + QuotePath(debugLogPath) +
		" --require-step8-pass" +
		" --output " + QuotePath(outputPath);

	REQUIRE(RunCommand(command) == 0);
	REQUIRE(std::filesystem::exists(outputPath));

	std::filesystem::remove_all(tempRoot);
}

TEST_CASE(
	"Sherpa Step 9 comparison reports matched reference timing and drain state",
	"[speech][sherpa][step9]")
{
	const auto tempRoot = CreateUniqueTempDirectory();
	const auto baselinePath = tempRoot / "step9-pass.sherpa-baseline.json";
	const auto referencePath = tempRoot / "step9-reference.json";
	const auto outputPath = tempRoot / "comparison.json";

	WriteTextFile(
		baselinePath,
		"{\n"
		"  \"decodedText\": \"请讲一个笑话\",\n"
		"  \"sampleRate\": 16000,\n"
		"  \"fbankFrameCount\": 176,\n"
		"  \"encoderFrameCount\": 176,\n"
		"  \"decodedTokenCount\": 4,\n"
		"  \"tokenIds\": \"601 499 838 2508\",\n"
		"  \"tokenPieces\": \"请 讲 一 个\",\n"
		"  \"sequenceStart\": 0,\n"
		"  \"sequenceEnd\": 55329,\n"
		"  \"cursorNextSequence\": 55329,\n"
		"  \"finalRemainingSamples\": 0,\n"
		"  \"finalFlush\": true,\n"
		"  \"finalDrainComplete\": true,\n"
		"  \"finalOutcome\": \"final_transcript\",\n"
		"  \"latencyMs\": 3460,\n"
		"  \"loopCount\": 347,\n"
		"  \"maxLoopCount\": 348\n"
		"}\n");

	WriteTextFile(
		referencePath,
		"{\n"
		"  \"text\": \"请讲一个笑话\",\n"
		"  \"sample_rate\": 16000,\n"
		"  \"fbank_frames\": 176,\n"
		"  \"encoder_frames\": 176,\n"
		"  \"token_count\": 4,\n"
		"  \"token_ids\": [601, 499, 838, 2508],\n"
		"  \"token_pieces\": [\"请\", \"讲\", \"一\", \"个\"],\n"
		"  \"sequence_start\": 0,\n"
		"  \"sequence_end\": 55329,\n"
		"  \"cursor_next\": 55329,\n"
		"  \"final_remaining_samples\": 0,\n"
		"  \"final_flush\": true,\n"
		"  \"final_drain_complete\": true,\n"
		"  \"final_outcome\": \"final_transcript\",\n"
		"  \"latency_ms\": 3460,\n"
		"  \"loop_count\": 347,\n"
		"  \"max_loop_count\": 348\n"
		"}\n");

	const auto scriptPath = ResolveProjectPath(
		std::filesystem::path("BlazeClawMfc") / "tools" / "compare_sherpa_baseline.py");
	REQUIRE(std::filesystem::exists(scriptPath));

	const std::string command =
		"python " + QuotePath(scriptPath) +
		" --baseline " + QuotePath(baselinePath) +
		" --reference " + QuotePath(referencePath) +
		" --output " + QuotePath(outputPath);

	REQUIRE(RunCommand(command) == 0);
	REQUIRE(std::filesystem::exists(outputPath));

	std::filesystem::remove_all(tempRoot);
}

TEST_CASE(
	"Sherpa Step 9 comparison reports reference output differences",
	"[speech][sherpa][step9]")
{
	const auto tempRoot = CreateUniqueTempDirectory();
	const auto baselinePath = tempRoot / "step9-different.sherpa-baseline.json";
	const auto referencePath = tempRoot / "step9-reference.json";
	const auto outputPath = tempRoot / "comparison.json";

	WriteTextFile(
		baselinePath,
		"{\n"
		"  \"decodedText\": \"请讲一个笑话\",\n"
		"  \"sampleRate\": 16000,\n"
		"  \"fbankFrameCount\": 176,\n"
		"  \"encoderFrameCount\": 176,\n"
		"  \"decodedTokenCount\": 4,\n"
		"  \"tokenIds\": \"601 499 838 2508\",\n"
		"  \"tokenPieces\": \"请 讲 一 个\",\n"
		"  \"sequenceEnd\": 55329,\n"
		"  \"cursorNextSequence\": 55329,\n"
		"  \"finalRemainingSamples\": 0,\n"
		"  \"finalFlush\": true,\n"
		"  \"finalDrainComplete\": true,\n"
		"  \"finalOutcome\": \"final_transcript\",\n"
		"  \"latencyMs\": 3460\n"
		"}\n");

	WriteTextFile(
		referencePath,
		"{\n"
		"  \"text\": \"请讲一个故事\",\n"
		"  \"sample_rate\": 16000,\n"
		"  \"fbank_frames\": 175,\n"
		"  \"encoder_frames\": 176,\n"
		"  \"token_count\": 4,\n"
		"  \"token_ids\": [601, 499, 838, 3000],\n"
		"  \"token_pieces\": [\"请\", \"讲\", \"一\", \"故事\"],\n"
		"  \"sequence_end\": 55329,\n"
		"  \"cursor_next\": 55000,\n"
		"  \"final_remaining_samples\": 329,\n"
		"  \"final_flush\": true,\n"
		"  \"final_drain_complete\": false,\n"
		"  \"final_outcome\": \"finite_stream_not_drained\",\n"
		"  \"latency_ms\": 3400\n"
		"}\n");

	const auto scriptPath = ResolveProjectPath(
		std::filesystem::path("BlazeClawMfc") / "tools" / "compare_sherpa_baseline.py");
	REQUIRE(std::filesystem::exists(scriptPath));

	const std::string command =
		"python " + QuotePath(scriptPath) +
		" --baseline " + QuotePath(baselinePath) +
		" --reference " + QuotePath(referencePath) +
		" --output " + QuotePath(outputPath);

	REQUIRE(RunCommand(command) == 0);
	REQUIRE(std::filesystem::exists(outputPath));

	std::filesystem::remove_all(tempRoot);
}

TEST_CASE(
	"Sherpa Step 8 checker rejects all-blank no-output baselines",
	"[speech][sherpa][step8]")
{
	const auto tempRoot = CreateUniqueTempDirectory();
	const auto baselinePath = tempRoot / "step8-fail.sherpa-baseline.json";

	WriteTextFile(
		baselinePath,
		"{\n"
		"  \"expectedText\": \"请讲一个笑话\",\n"
		"  \"decodedText\": \"\",\n"
		"  \"joinerCallCount\": 216,\n"
		"  \"blankTokenCount\": 216,\n"
		"  \"decodedTokenCount\": 0,\n"
		"  \"finalOutcome\": \"no_tokens_emitted\",\n"
		"  \"hasSegment\": false,\n"
		"  \"fallbackUsed\": false\n"
		"}\n");

	const auto scriptPath = ResolveProjectPath(
		std::filesystem::path("BlazeClawMfc") / "tools" / "compare_sherpa_baseline.py");
	REQUIRE(std::filesystem::exists(scriptPath));

	const std::string command =
		"python " + QuotePath(scriptPath) +
		" --baseline " + QuotePath(baselinePath) +
		" --require-step8-pass";

	REQUIRE(RunCommand(command) != 0);

	std::filesystem::remove_all(tempRoot);
}
