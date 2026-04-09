#include "core/diagnostics/DiagnosticsRegressionComparator.h"

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

namespace {

	bool IsStrictDiagnosticsRegressionGateEnabled()
	{
		char* value = nullptr;
		size_t length = 0;
		if (_dupenv_s(
			&value,
			&length,
			"BLAZECLAW_DIAGNOSTICS_REGRESSION_STRICT") != 0 ||
			value == nullptr ||
			length == 0)
		{
			if (value != nullptr)
			{
				free(value);
			}
			return false;
		}

		std::string normalized(value);
		free(value);
		std::transform(
			normalized.begin(),
			normalized.end(),
			normalized.begin(),
			[](const unsigned char ch)
			{
				return static_cast<char>(std::tolower(ch));
			});

		return normalized == "1" ||
			normalized == "true" ||
			normalized == "on";
	}

	void EnforceDiagnosticsRegressionGate(
		const blazeclaw::core::DiagnosticsRegressionComparator::ComparisonResult& result)
	{
		if (result.equivalent)
		{
			SUCCEED(result.summary);
			return;
		}

		const bool strictGateEnabled = IsStrictDiagnosticsRegressionGateEnabled();
		if (strictGateEnabled)
		{
			REQUIRE(result.equivalent);
			return;
		}

		WARN(result.summary);
		SUCCEED("diagnostics regression gate configured as non-blocking");
	}

} // namespace

TEST_CASE(
	"Diagnostics comparator reports stable parity when selected fields match",
	"[diagnostics][regression][parity]") {
	blazeclaw::core::DiagnosticsSnapshot before;
	before.runtimeRunning = true;
	before.gatewayWarning = "";
	before.emailFallbackAttempts = 2;
	before.emailFallbackSuccess = 1;
	before.emailFallbackFailure = 1;
	before.emailCapabilityState = "degraded";
	before.skillsCatalogEntries = 12;
	before.skillsPromptIncluded = 8;
	before.hooksDispatches = 4;
	before.hooksPolicyBlocked = 1;
	before.hooksDriftDetected = 0;
	before.hooksLastDriftReason = "";
	before.embeddedTotalRuns = 6;
	before.embeddedTaskDeltaTransitions = 22;

	auto after = before;

	blazeclaw::core::DiagnosticsRegressionComparator comparator;
	const auto result = comparator.CompareSelectedFields(before, after);

	REQUIRE(result.equivalent);
	REQUIRE(result.differences.empty());
	REQUIRE(result.summary == "diagnostics parity stable");
	EnforceDiagnosticsRegressionGate(result);
}

TEST_CASE(
	"Diagnostics comparator reports drift for changed selected fields",
	"[diagnostics][regression][parity]") {
	blazeclaw::core::DiagnosticsSnapshot before;
	before.runtimeRunning = true;
	before.emailFallbackAttempts = 2;
	before.skillsCatalogEntries = 10;
	before.hooksPolicyBlocked = 1;
	before.embeddedTaskDeltaTransitions = 8;

	blazeclaw::core::DiagnosticsSnapshot after = before;
	after.runtimeRunning = false;
	after.emailFallbackAttempts = 3;
	after.skillsCatalogEntries = 11;
	after.hooksPolicyBlocked = 2;
	after.embeddedTaskDeltaTransitions = 9;

	blazeclaw::core::DiagnosticsRegressionComparator comparator;
	const auto result = comparator.CompareSelectedFields(before, after);

	REQUIRE_FALSE(result.equivalent);
	REQUIRE(result.differences.size() >= 5);
	REQUIRE(result.summary.find("diagnostics parity drift detected") != std::string::npos);

	const auto hasRuntimeChange = std::any_of(
		result.differences.begin(),
		result.differences.end(),
		[](const blazeclaw::core::DiagnosticsRegressionComparator::Difference& item) {
			return item.field == "runtime.running";
		});
	REQUIRE(hasRuntimeChange);
}
