#include "pch.h"

#include "../src/core/ServiceManagerSpeechRuntimeHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

using namespace blazeclaw::core::servicemanager_speech;

TEST_CASE("SpeechRuntimeEnabled respects environment values", "[servicemanager][speech]")
{
	const wchar_t* envName = L"BLAZECLAW_SPEECH_RUNTIME_ENABLED";

	// Ensure unset -> false
	_wputenv_s(envName, L"");
	REQUIRE(SpeechRuntimeEnabled() == false);

	_wputenv_s(envName, L"true");
	REQUIRE(SpeechRuntimeEnabled() == true);

	_wputenv_s(envName, L"1");
	REQUIRE(SpeechRuntimeEnabled() == true);

	_wputenv_s(envName, L"0");
	REQUIRE(SpeechRuntimeEnabled() == false);

	_wputenv_s(envName, L"false");
	REQUIRE(SpeechRuntimeEnabled() == false);

	_wputenv_s(envName, L"");
}

TEST_CASE("StartSpeechRuntimeAsync is callable and no-throw", "[servicemanager][speech]")
{
	REQUIRE_NOTHROW(StartSpeechRuntimeAsync());
}
