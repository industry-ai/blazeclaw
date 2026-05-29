#include "pch.h"

#include "../src/core/runtime/SpeechRecognition/SpeechCudaCompatibilityGuard.h"
#include "../src/core/runtime/SpeechRecognition/SpeechModelLayoutProbe.h"
#include "../src/core/runtime/SpeechRecognition/engines/SherpaZipformerStreamingEngine.h"

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
			(std::wstring(L"blazeclaw-speech-cuda-guard-") + suffix + L"-" + tick);
		std::filesystem::create_directories(root);
		return root;
	}

	void TouchOnnxStub(const std::filesystem::path& path)
	{
		std::ofstream out(path, std::ios::binary);
		const char onnxStub[] = { 'O','N','N','X','S','T','U','B' };
		out.write(onnxStub, sizeof(onnxStub));
	}

	blazeclaw::core::speechrecognition::SpeechModelLayoutProbeResult BuildSherpaStubLayout(
		const std::filesystem::path& root)
	{
		TouchOnnxStub(root / L"encoder-epoch-99-avg-1.int8.onnx");
		TouchOnnxStub(root / L"decoder-epoch-99-avg-1.int8.onnx");
		TouchOnnxStub(root / L"joiner-epoch-99-avg-1.int8.onnx");

		std::ofstream tokens(root / L"tokens.txt", std::ios::binary);
		tokens << "<blk> 0\n<sos/eos> 1\n<unk> 2\n▁讲 3\n";
		tokens.close();

		return blazeclaw::core::speechrecognition::ProbeSpeechModelLayout(root);
	}

} // namespace

TEST_CASE("Speech CUDA compatibility guard accepts empty observed module set", "[speech][cuda][guard]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto result = EvaluateSpeechCudaCompatibilityGuard({});

	REQUIRE(result.compatible);
	REQUIRE(result.observed.empty());
	REQUIRE(result.violations.empty());
	REQUIRE(result.reason.empty());
}

TEST_CASE("Speech CUDA compatibility guard reports incompatible loaded major", "[speech][cuda][guard]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto result = EvaluateSpeechCudaCompatibilityGuard({
		SpeechCudaLoadedModule{
			.label = "cublas",
			.major = 13,
			.expectedMajor = 12,
		},
		SpeechCudaLoadedModule{
			.label = "cudnn",
			.major = 9,
			.expectedMajor = 9,
		},
	});

	REQUIRE_FALSE(result.compatible);
	REQUIRE(result.observed.size() == 2);
	REQUIRE(result.violations.size() == 1);
	REQUIRE(result.reason.find("compatibility_guard_blocked") != std::string::npos);
	REQUIRE(result.reason.find("cublas=13") != std::string::npos);
	REQUIRE(result.reason.find("cublas major=13 expected=12") != std::string::npos);
}

TEST_CASE("Sherpa CUDA provider selection honors latched compatibility guard", "[speech][cuda][sherpa]")
{
	using namespace blazeclaw::core::speechrecognition;

	const auto root = CreateUniqueTempDirectory(L"sherpa-latched");
	const auto layout = BuildSherpaStubLayout(root);

	engines::SherpaZipformerStreamingEngine engine;
	engines::SherpaZipformerStreamingEngine::ExecutionProviderStatus status;
	std::string loadError;

	const bool loaded = engine.Load(
		root,
		layout,
		engines::SherpaZipformerStreamingEngine::ExecutionProviderOptions{
			.cudaEnabled = true,
			.threads = 1,
			.executionMode = "sequential",
			.cudaCompatibilityGuardLatched = true,
			.cudaCompatibilityGuardLatchedReason = "test_latched_reason",
		},
		status,
		loadError);

	REQUIRE_FALSE(status.cudaExecutionProviderEnabled);
	REQUIRE(status.effectiveExecutionProvider == "cpu");
	REQUIRE(status.cudaCompatibilityGuardLatched);
	REQUIRE(status.cudaExecutionProviderReason.find("compatibility_guard_latched_in_process") != std::string::npos);
	REQUIRE(status.cudaExecutionProviderReason.find("test_latched_reason") != std::string::npos);

	if (loaded) {
		SUCCEED("Sherpa stub unexpectedly loaded; provider latch assertions still passed.");
	}

	std::filesystem::remove_all(root);
}
