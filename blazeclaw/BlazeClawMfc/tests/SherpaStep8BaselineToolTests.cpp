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
