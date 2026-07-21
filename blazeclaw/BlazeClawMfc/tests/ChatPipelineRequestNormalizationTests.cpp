#include "gateway/ChatPipelineRequestNormalization.h"
#include "gateway/GatewaySessionRegistry.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::GatewaySessionRegistry;
using blazeclaw::gateway::handlers::runtime::ChatPipelineRequestNormalization;

TEST_CASE(
	"ChatPipelineRequestNormalization resolves subscriber route fields",
	"[chat-pipeline][request-normalization]") {
	GatewaySessionRegistry sessionRegistry;
	ChatPipelineRequestNormalization normalization(sessionRegistry);

	const auto route = normalization.NormalizeSubscriberRoute(
		std::optional<std::string>{
			"{\"sessionId\":\"\",\"connectionId\":\"\",\"clientConnectionId\":\"ws-1\"}"});

	REQUIRE(route.sessionId == "main");
	REQUIRE(route.connectionId == "ws-1");
}

TEST_CASE(
	"ChatPipelineRequestNormalization defaults session key routes to main",
	"[chat-pipeline][request-normalization]") {
	const auto route = ChatPipelineRequestNormalization::NormalizeSessionKeyRoute(
		std::optional<std::string>{"{\"sessionKey\":\"\"}"});

	REQUIRE(route.requestedSessionKey.empty());
	REQUIRE(route.sessionKey == "main");
}

TEST_CASE(
	"ChatPipelineRequestNormalization bounds poll limits",
	"[chat-pipeline][request-normalization]") {
	const auto defaultRoute = ChatPipelineRequestNormalization::NormalizePollRoute(std::nullopt);
	REQUIRE(defaultRoute.session.sessionKey == "main");
	REQUIRE(defaultRoute.limit == ChatPipelineRequestNormalization::kDefaultPollLimit);

	const auto cappedRoute = ChatPipelineRequestNormalization::NormalizePollRoute(
		std::optional<std::string>{"{\"sessionKey\":\"team\",\"limit\":500}"});
	REQUIRE(cappedRoute.session.sessionKey == "team");
	REQUIRE(cappedRoute.limit == ChatPipelineRequestNormalization::kMaxPollLimit);

	const auto flooredRoute = ChatPipelineRequestNormalization::NormalizePollRoute(
		std::optional<std::string>{"{\"limit\":0}"});
	REQUIRE(flooredRoute.limit == ChatPipelineRequestNormalization::kMinPollLimit);
}

TEST_CASE(
	"ChatPipelineRequestNormalization extracts abort and inject fields",
	"[chat-pipeline][request-normalization]") {
	const auto abortRoute = ChatPipelineRequestNormalization::NormalizeAbortRoute(
		std::optional<std::string>{"{\"sessionKey\":\"ops\",\"runId\":\"run-9\"}"});
	REQUIRE(abortRoute.session.sessionKey == "ops");
	REQUIRE(abortRoute.requestedRunId == "run-9");

	const auto injectRoute = ChatPipelineRequestNormalization::NormalizeInjectRoute(
		std::optional<std::string>{
			"{\"sessionKey\":\"ops\",\"message\":\"hello\",\"label\":\"note\"}"});
	REQUIRE(injectRoute.session.sessionKey == "ops");
	REQUIRE(injectRoute.message == "hello");
	REQUIRE(injectRoute.label == "note");
}
