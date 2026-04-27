#include "gateway/SendPolicyResolver.h"
#include "gateway/RuntimeSequencingPolicy.h"
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

TEST_CASE("Policy parity: ordered nano-pdf alias expands to generate then edit", "[policy][ordered][nano-pdf]") {
	const std::vector<ToolCatalogEntry> tools = {
		ToolCatalogEntry{.id = "baidu-search.search.web", .label = "Baidu Search", .category = "web", .enabled = true },
		ToolCatalogEntry{.id = "web_browsing.fetch.content", .label = "Web Browsing Fetch", .category = "web", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.edit", .label = "Nano PDF Edit", .category = "document", .enabled = true },
	};

	const std::string prompt =
		"Please strictly execute in order: 1. Call `baidu-search` to search for "
		"\"2024 commercialization progress of solid-state batteries\"; 2. Call "
		"`web-browsing` to deeply read the main text of the top two search results "
		"and extract core data; 3. Based on the extracted data, write a well-structured "
		"business brief, and call `nano-pdf` to format it into a professional PDF file "
		"saved locally at '/tmp/Battery_Report.pdf'";

	const auto preflight = RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
		prompt,
		tools,
		{});

	REQUIRE(preflight.enforced);
	REQUIRE(preflight.strictAllowlist);
	REQUIRE(preflight.missingTargets.empty());
	REQUIRE(
		std::find(
			preflight.resolvedToolTargets.begin(),
			preflight.resolvedToolTargets.end(),
			std::string("nano_pdf.generate")) != preflight.resolvedToolTargets.end());
	REQUIRE(
		std::find(
			preflight.resolvedToolTargets.begin(),
			preflight.resolvedToolTargets.end(),
			std::string("nano_pdf.edit")) != preflight.resolvedToolTargets.end());
}

TEST_CASE("Policy parity: explicit nano_pdf.edit with inputPath does not inject generate", "[policy][ordered][nano-pdf]") {
	const std::vector<ToolCatalogEntry> tools = {
		ToolCatalogEntry{.id = "nano_pdf.generate", .label = "Nano PDF Generate", .category = "document", .enabled = true },
		ToolCatalogEntry{.id = "nano_pdf.edit", .label = "Nano PDF Edit", .category = "document", .enabled = true },
	};
	const std::string prompt =
		"Please strictly execute in order: 1. call `nano_pdf.edit` with "
		"inputPath='/tmp/existing.pdf' pageIndex=0 instruction='polish headings' "
		"outputPath='/tmp/final.pdf'";

	const auto preflight = RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
		prompt,
		tools,
		{});

	REQUIRE(preflight.enforced);
	REQUIRE(preflight.missingTargets.empty());
	REQUIRE(preflight.resolvedToolTargets.size() == 1);
	REQUIRE(preflight.resolvedToolTargets.front() == "nano_pdf.edit");
}
