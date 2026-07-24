#include "pch.h"

#include "../src/core/ServiceManagerLifecycleHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <Windows.h>

TEST_CASE("AppendStartupTrace is callable and does not throw", "[servicemanager][lifecycle]")
{
	// Primary assertion is that the forwarder can be invoked without crashing.
	REQUIRE_NOTHROW(blazeclaw::core::servicemanager_lifecycle::AppendStartupTrace("unit-test-stage"));
}

TEST_CASE("SuppressStartupMigrationsFromEnv respects environment", "[servicemanager][lifecycle]")
{
	const wchar_t* envName = L"BLAZECLAW_GATEWAY_SUPPRESS_STARTUP_MIGRATIONS";

	// Ensure unset -> false
	SetEnvironmentVariableW(envName, nullptr);
	REQUIRE(blazeclaw::core::servicemanager_lifecycle::SuppressStartupMigrationsFromEnv() == false);

	// Set explicit true values
	SetEnvironmentVariableW(envName, L"true");
	REQUIRE(blazeclaw::core::servicemanager_lifecycle::SuppressStartupMigrationsFromEnv() == true);

	SetEnvironmentVariableW(envName, L"0");
	REQUIRE(blazeclaw::core::servicemanager_lifecycle::SuppressStartupMigrationsFromEnv() == false);

	// Clean up
	SetEnvironmentVariableW(envName, nullptr);
}
