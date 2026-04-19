#include "gateway/GatewayProtocolModels.h"
#include "gateway/GatewayJsonBuilder.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::JsonPayloadCount;
using blazeclaw::gateway::JsonPayloadExists;
using blazeclaw::gateway::JsonPayloadFoundCount;
using blazeclaw::gateway::JsonPayloadPathExists;
using blazeclaw::gateway::protocol::ErrorResponse;
using blazeclaw::gateway::protocol::OkResponse;
using blazeclaw::gateway::protocol::RequestFrame;

TEST_CASE("ErrorResponse sets id ok payload and error", "[gateway][protocol]") {
	const RequestFrame request{.id = "r1", .method = "test.method", .paramsJson = std::nullopt};
	const auto res = ErrorResponse(request, "code_x", "msg_y");

	REQUIRE(res.id == "r1");
	REQUIRE(res.ok == false);
	REQUIRE(!res.payloadJson.has_value());
	REQUIRE(res.error.has_value());
	REQUIRE(res.error->code == "code_x");
	REQUIRE(res.error->message == "msg_y");
	REQUIRE(!res.error->detailsJson.has_value());
	REQUIRE(res.error->retryable.has_value());
	REQUIRE(*res.error->retryable == false);
	REQUIRE(!res.error->retryAfterMs.has_value());
}

TEST_CASE("JsonPayloadExists and JsonPayloadCount shapes", "[gateway][protocol]") {
	REQUIRE(JsonPayloadExists(true) == R"({"exists":true})");
	REQUIRE(JsonPayloadExists(false) == R"({"exists":false})");
	REQUIRE(JsonPayloadCount(7) == R"({"count":7})");
	REQUIRE(JsonPayloadFoundCount(true, 2) == R"({"found":true,"count":2})");
}

TEST_CASE("JsonPayloadPathExists escapes path", "[gateway][protocol]") {
	REQUIRE(JsonPayloadPathExists(R"(a"b)", true) == R"({"path":"a\"b","exists":true})");
}

TEST_CASE("OkResponse wraps JsonPayloadPathExists", "[gateway][protocol]") {
	const RequestFrame request{.id = "r2", .method = "gateway.agents.files.exists", .paramsJson = "{}"};
	const auto res = OkResponse(request, JsonPayloadPathExists("p", false));

	REQUIRE(res.ok);
	REQUIRE(res.payloadJson == R"({"path":"p","exists":false})");
}
