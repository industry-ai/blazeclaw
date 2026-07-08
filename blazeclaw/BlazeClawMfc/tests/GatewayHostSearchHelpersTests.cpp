#include "gateway/GatewayHostSearchHelpers.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

using blazeclaw::gateway::host_search_helpers::BuildMemorySearchEnvelope;
using blazeclaw::gateway::host_search_helpers::ToLowerCopy;
using blazeclaw::gateway::host_search_helpers::TruncateForMatch;

TEST_CASE("GatewayHost search helpers lowercase query deterministically", "[gateway][search][helpers]") {
	REQUIRE(ToLowerCopy("ABC xyz") == "abc xyz");
	REQUIRE(ToLowerCopy("MiXeD_123") == "mixed_123");
}

TEST_CASE("GatewayHost search helpers truncate for envelope preview", "[gateway][search][helpers]") {
	REQUIRE(TruncateForMatch("short", 10) == "short");
	REQUIRE(TruncateForMatch("1234567890", 10) == "1234567890");
	REQUIRE(TruncateForMatch("12345678901", 10) == "1234567890...");
}

TEST_CASE("GatewayHost search helpers build search envelope payload", "[gateway][search][helpers]") {
	const std::vector<std::string> matches = {
		"row-a",
		"row-b"
	};
	const std::string payload = BuildMemorySearchEnvelope("session-1", matches);
	const nlohmann::json parsed = nlohmann::json::parse(payload, nullptr, false);

	REQUIRE(parsed.is_object());
	REQUIRE(parsed.value("sessionKey", std::string()) == "session-1");
	REQUIRE(parsed.value("count", 0) == 2);
	REQUIRE(parsed.contains("matches"));
	REQUIRE(parsed["matches"].is_array());
	REQUIRE(parsed["matches"].size() == 2);
	REQUIRE(parsed["matches"][0].value("text", std::string()) == "row-a");
	REQUIRE(parsed["matches"][1].value("text", std::string()) == "row-b");
}
