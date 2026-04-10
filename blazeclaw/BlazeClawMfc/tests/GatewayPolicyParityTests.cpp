#include "gateway/SendPolicyResolver.h"
#include "gateway/ToolPolicyPipeline.h"
#include "gateway/TranscriptPolicyResolver.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

TEST_CASE("Policy parity: send policy denies empty message without attachments", "[policy][send]") {
	const auto decision = SendPolicyResolver::Evaluate(
		"main",
		"",
		false,
		{});

	REQUIRE_FALSE(decision.allowed);
	REQUIRE(decision.reasonCode == "denied_send");
}

TEST_CASE("Policy parity: tool policy allowlist honors enabled tools", "[policy][tool]") {
	const std::vector<ToolCatalogEntry> tools = {
		ToolCatalogEntry{.id = "weather.lookup", .label = "Weather", .category = "ops", .enabled = true },
		ToolCatalogEntry{.id = "email.schedule", .label = "Email", .category = "ops", .enabled = true },
	};
	const auto decision = ToolPolicyPipeline::Build(
		true,
		{ "weather.lookup", "missing.tool" },
		tools);

	REQUIRE_FALSE(decision.allowAll);
	REQUIRE(decision.reasonCode == "tool_policy_allowlist");
	REQUIRE(decision.allowedTargets.size() == 1);
	REQUIRE(decision.allowedTargets.front() == "weather.lookup");
}

TEST_CASE("Policy parity: transcript policy strips think blocks for deepseek", "[policy][transcript]") {
	const auto decision = TranscriptPolicyResolver::Resolve(
		"Hello <think>hidden reasoning</think> world",
		"deepseek");

	REQUIRE(decision.applied);
	REQUIRE(decision.reasonCode == "transcript_policy_applied");
	REQUIRE(decision.sanitizedMessage.find("<think>") == std::string::npos);
}
