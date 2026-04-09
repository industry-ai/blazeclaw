#include "core/providers/CDeepSeekClient.h"

#include <catch2/catch_all.hpp>

TEST_CASE("DeepSeek SSE parser aggregates chunked lines deterministically", "[deepseek][sse]") {
	blazeclaw::core::CDeepSeekClient client;
	const std::string responseBody =
		"data: {\"choices\":[{\"delta\":{\"content\":\"Hel\"}}]}\n"
		"data: {\"choices\":[{\"delta\":{\"content\":\"lo\"}}]}\n"
		"data: [DONE]\n";

	const auto deltas = client.ParseAssistantDeltasForTest(responseBody);

	REQUIRE(deltas.size() == 2);
	REQUIRE(deltas[0] == "Hel");
	REQUIRE(deltas[1] == "Hello");
}

TEST_CASE("DeepSeek SSE parser flushes trailing line without newline", "[deepseek][sse]") {
	blazeclaw::core::CDeepSeekClient client;
	const std::string responseBody =
		"data: {\"choices\":[{\"delta\":{\"content\":\"A\"}}]}\n"
		"data: {\"choices\":[{\"delta\":{\"content\":\"B\"}}]}";

	const auto deltas = client.ParseAssistantDeltasForTest(responseBody);

	REQUIRE(deltas.size() == 2);
	REQUIRE(deltas[1] == "AB");
}

TEST_CASE("DeepSeek SSE parser handles escaped content and ignores empty pieces", "[deepseek][sse]") {
	blazeclaw::core::CDeepSeekClient client;
	const std::string responseBody =
		"data: {\"choices\":[{\"delta\":{\"content\":\"line1\\n\"}}]}\n"
		"data: {\"choices\":[{\"delta\":{\"content\":\"\"}}]}\n"
		"data: {\"choices\":[{\"delta\":{\"content\":\"line2\"}}]}\n"
		"data: [DONE]\n";

	const auto deltas = client.ParseAssistantDeltasForTest(responseBody);

	REQUIRE(deltas.size() == 2);
	REQUIRE(deltas[0] == "line1\n");
	REQUIRE(deltas[1] == "line1\nline2");
}
