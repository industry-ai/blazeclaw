#include "gateway/Telemetry.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::NormalizePayloadObject;
using blazeclaw::gateway::JsonString;

TEST_CASE("NormalizePayloadObject keeps object payload shape", "[telemetry]") {
    const std::string payload = "  {\"key\":\"value\",\"n\":1}  ";
    const std::string normalized = NormalizePayloadObject(payload);

    REQUIRE(normalized == "{\"key\":\"value\",\"n\":1}");
}

TEST_CASE("NormalizePayloadObject wraps non-object payload as raw", "[telemetry]") {
    const std::string payload = "not-json-object";
    const std::string normalized = NormalizePayloadObject(payload);

    REQUIRE(normalized == "{\"raw\":\"not-json-object\"}");
}

TEST_CASE("NormalizePayloadObject returns empty object for whitespace", "[telemetry]") {
    const std::string payload = " \t\r\n  ";
    const std::string normalized = NormalizePayloadObject(payload);

    REQUIRE(normalized == "{}");
}

TEST_CASE("JsonString escapes quotes and newlines", "[telemetry]") {
    const std::string input = "line \"quoted\"\nnext line";
    const std::string escaped = JsonString(input);

    REQUIRE(escaped == "\"line \\\"quoted\\\"\\nnext line\"");
}
