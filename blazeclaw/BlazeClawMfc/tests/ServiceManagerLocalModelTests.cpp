#include "pch.h"

#include "../src/core/ServiceManagerLocalModelHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

using namespace blazeclaw::core::servicemanager_localmodel;

TEST_CASE("ResolveLocalModelActivationFromEnv respects environment values", "[servicemanager][localmodel]")
{
	const wchar_t* envName = L"BLAZECLAW_LOCAL_MODEL_ACTIVATION";

	// Ensure unset -> false
	_wputenv_s(envName, L"");
	REQUIRE(ResolveLocalModelActivationFromEnv() == false);

	// True variants
	_wputenv_s(envName, L"true");
	REQUIRE(ResolveLocalModelActivationFromEnv() == true);

	_wputenv_s(envName, L"1");
	REQUIRE(ResolveLocalModelActivationFromEnv() == true);

	// False variants
	_wputenv_s(envName, L"0");
	REQUIRE(ResolveLocalModelActivationFromEnv() == false);

	_wputenv_s(envName, L"false");
	REQUIRE(ResolveLocalModelActivationFromEnv() == false);

	// Clean up
	_wputenv_s(envName, L"");
}

TEST_CASE("BuildLocalModelActivationReason prefers config reason then env", "[servicemanager][localmodel]")
{
	const std::optional<std::string> cfg = std::string("config_reason_x");
	REQUIRE(BuildLocalModelActivationReason(cfg, false) == "config_reason_x");

	const std::optional<std::string> none = std::nullopt;
	REQUIRE(BuildLocalModelActivationReason(none, true) == "env_forced_activation");
	REQUIRE(BuildLocalModelActivationReason(none, false).empty());
}
