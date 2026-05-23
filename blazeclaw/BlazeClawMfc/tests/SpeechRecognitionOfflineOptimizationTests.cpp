#include "pch.h"

#include "../src/core/runtime/SpeechRecognition/SpeechRecognitionRuntime.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {
	std::string NarrowPath(const std::filesystem::path& path)
	{
		return path.string();
	}

	std::filesystem::path CreateUniqueTempDirectory(const wchar_t* suffix)
	{
		const auto tick = std::to_wstring(
			std::chrono::steady_clock::now().time_since_epoch().count());
		const auto root = std::filesystem::temp_directory_path() /
			(std::wstring(L"blazeclaw-stt-offline-") + suffix + L"-" + tick);
		std::filesystem::create_directories(root);
		return root;
	}
}

TEST_CASE("Offline STT optimization deduplicates failed roots", "[speech][offline_opt]")
{
	auto tempRoot = CreateUniqueTempDirectory(L"dedupe");
	blazeclaw::config::SpeechRecognitionConfig config;

	const std::vector<std::wstring> roots = {
		tempRoot.wstring(),
		tempRoot.wstring(),
	};

	const auto result = blazeclaw::core::speechrecognition::OptimizeSpeechRecognitionModelsOffline(
		config,
		roots);

	REQUIRE_FALSE(result.success);
	REQUIRE(result.optimizedRoots.empty());
	REQUIRE(result.failedRoots.size() == 1);
	REQUIRE(result.failedRoots.front() == NarrowPath(tempRoot));

	std::filesystem::remove_all(tempRoot);
}

TEST_CASE("Offline STT optimization fails when no model variants are present", "[speech][offline_opt]")
{
	auto tempRoot = CreateUniqueTempDirectory(L"no-variants");
	blazeclaw::config::SpeechRecognitionConfig config;

	const auto result = blazeclaw::core::speechrecognition::OptimizeSpeechRecognitionModelsOffline(
		config,
		std::vector<std::wstring>{ tempRoot.wstring() });

	REQUIRE_FALSE(result.success);
	REQUIRE(result.optimizedRoots.empty());
	REQUIRE(result.failedRoots.size() == 1);
	REQUIRE(result.failedRoots.front() == NarrowPath(tempRoot));

	std::filesystem::remove_all(tempRoot);
}
