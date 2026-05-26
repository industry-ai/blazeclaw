#include "pch.h"

#include "../src/core/runtime/SpeechRecognition/SpeechModelLayoutProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

	std::filesystem::path CreateUniqueTempDirectory(const wchar_t* suffix)
	{
		const auto tick = std::to_wstring(
			std::chrono::steady_clock::now().time_since_epoch().count());
		const auto root = std::filesystem::temp_directory_path() /
			(std::wstring(L"blazeclaw-speech-layout-") + suffix + L"-" + tick);
		std::filesystem::create_directories(root);
		return root;
	}

	void TouchFile(const std::filesystem::path& path)
	{
		std::ofstream out(path, std::ios::binary);
		out << "stub";
	}

} // namespace

TEST_CASE("SpeechModelLayoutProbe detects qwen decoder_init_step layout", "[speech][layout]")
{
	const auto root = CreateUniqueTempDirectory(L"qwen");
	TouchFile(root / L"encoder.int4.onnx");
	TouchFile(root / L"decoder_init.int4.onnx");

	const auto result = blazeclaw::core::speechrecognition::ProbeSpeechModelLayout(root);

	REQUIRE(result.kind ==
		blazeclaw::core::speechrecognition::SpeechModelLayoutKind::QwenDecoderInitStep);
	REQUIRE(result.layout == "qwen_decoder_init_step");
	REQUIRE(!result.availableQwenVariants.empty());
	REQUIRE(result.missingArtifacts.empty());

	std::filesystem::remove_all(root);
}

TEST_CASE("SpeechModelLayoutProbe detects sherpa zipformer transducer layout", "[speech][layout]")
{
	const auto root = CreateUniqueTempDirectory(L"sherpa");
	TouchFile(root / L"encoder-epoch-99-avg-1.onnx");
	TouchFile(root / L"decoder-epoch-99-avg-1.onnx");
	TouchFile(root / L"joiner-epoch-99-avg-1.onnx");
	TouchFile(root / L"tokens.txt");

	const auto result = blazeclaw::core::speechrecognition::ProbeSpeechModelLayout(root);

	REQUIRE(result.kind ==
		blazeclaw::core::speechrecognition::SpeechModelLayoutKind::SherpaZipformerTransducer);
	REQUIRE(result.layout == "sherpa_zipformer_transducer");
	REQUIRE(result.missingArtifacts.empty());
	REQUIRE(!result.sherpaEncoderPath.empty());
	REQUIRE(!result.sherpaDecoderPath.empty());
	REQUIRE(!result.sherpaJoinerPath.empty());
	REQUIRE(!result.sherpaTokensPath.empty());

	std::filesystem::remove_all(root);
}

TEST_CASE("SpeechModelLayoutProbe reports incomplete sherpa artifacts", "[speech][layout]")
{
	const auto root = CreateUniqueTempDirectory(L"incomplete");
	TouchFile(root / L"encoder-epoch-99-avg-1.onnx");
	TouchFile(root / L"tokens.txt");

	const auto result = blazeclaw::core::speechrecognition::ProbeSpeechModelLayout(root);

	REQUIRE(result.kind ==
		blazeclaw::core::speechrecognition::SpeechModelLayoutKind::Unknown);
	REQUIRE(result.layout == "incomplete_sherpa_zipformer_transducer");
	REQUIRE_FALSE(result.missingArtifacts.empty());
	REQUIRE(std::find(
		result.missingArtifacts.begin(),
		result.missingArtifacts.end(),
		"decoder-*.onnx") != result.missingArtifacts.end());
	REQUIRE(std::find(
		result.missingArtifacts.begin(),
		result.missingArtifacts.end(),
		"joiner-*.onnx") != result.missingArtifacts.end());

	std::filesystem::remove_all(root);
}
