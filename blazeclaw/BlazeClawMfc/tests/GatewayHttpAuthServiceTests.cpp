#include "gateway/GatewayHttpAuthService.h"

#include <catch2/catch_all.hpp>

#include <string>

using blazeclaw::gateway::GatewayHttpAuthDecisionResult;
using blazeclaw::gateway::GatewayHttpAuthPolicyCallbacks;
using blazeclaw::gateway::GatewayHttpAuthRequestContext;
using blazeclaw::gateway::GatewayHttpAuthService;

TEST_CASE("GatewayHttpAuthService IsCanvasPath mirrors OpenClaw route coverage", "[gateway][http-auth][phase1]") {
	REQUIRE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/a2ui"));
	REQUIRE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/a2ui/index.html"));
	REQUIRE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/canvas"));
	REQUIRE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/canvas/session/test"));
	REQUIRE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/ws"));

	REQUIRE_FALSE(GatewayHttpAuthService::IsCanvasPath("/__openclaw__/ws/sub"));
	REQUIRE_FALSE(GatewayHttpAuthService::IsCanvasPath("/gateway"));
}

TEST_CASE("GatewayHttpAuthService GetBearerToken handles authorization header case-insensitively", "[gateway][http-auth][phase3]") {
	std::unordered_map<std::string, std::string> headers;
	headers.insert_or_assign("Authorization", "Bearer abc-token");
	const auto token = GatewayHttpAuthService::GetBearerToken(headers);
	REQUIRE(token.has_value());
	REQUIRE(token.value() == "abc-token");

	headers.insert_or_assign("authorization", "bearer   xyz");
	const auto token2 = GatewayHttpAuthService::GetBearerToken(headers);
	REQUIRE(token2.has_value());
	REQUIRE(token2.value() == "xyz");
}

TEST_CASE("GatewayHttpAuthService malformed scoped path is unauthorized before auth checks", "[gateway][http-auth][phase2]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas/session/bad";
	context.malformedScopedPath = true;
	context.headers.insert_or_assign("Authorization", "Bearer should-not-run");

	bool authorizeCalled = false;
	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.authorizeBearer = [&authorizeCalled](
		const std::string&,
		const GatewayHttpAuthRequestContext&,
		std::string&) {
			authorizeCalled = true;
			return true;
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE_FALSE(result.ok);
	REQUIRE(result.reason == "unauthorized");
	REQUIRE(result.branch == "malformed_path");
	REQUIRE_FALSE(authorizeCalled);
}

TEST_CASE("GatewayHttpAuthService bearer branch succeeds with unified policy context", "[gateway][http-auth][phase3]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";
	context.headers.insert_or_assign("authorization", "Bearer token-1");
	context.remoteIp = "10.0.0.5";
	context.trustedProxies = { "127.0.0.1", "::1" };
	context.allowRealIpFallback = true;
	context.browserOriginPolicy = "localhost-only";

	bool sawExpectedContext = false;
	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.checkRateLimit = [](const GatewayHttpAuthRequestContext&, std::string&) {
		return true;
		};
	callbacks.authorizeBearer = [&sawExpectedContext](
		const std::string& token,
		const GatewayHttpAuthRequestContext& ctx,
		std::string&) {
			sawExpectedContext =
				token == "token-1" &&
				ctx.remoteIp == "10.0.0.5" &&
				ctx.allowRealIpFallback &&
				ctx.browserOriginPolicy == "localhost-only" &&
				ctx.trustedProxies.size() == 2;
			return true;
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE(result.ok);
	REQUIRE(result.branch == "bearer_ok");
	REQUIRE(sawExpectedContext);
}

TEST_CASE("GatewayHttpAuthService bearer failure returns bearer_fail branch", "[gateway][http-auth][phase3]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";
	context.headers.insert_or_assign("Authorization", "Bearer denied-token");

	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.authorizeBearer = [](
		const std::string&,
		const GatewayHttpAuthRequestContext&,
		std::string& failureReasonOut) {
			failureReasonOut = "invalid_token";
			return false;
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE_FALSE(result.ok);
	REQUIRE(result.reason == "invalid_token");
	REQUIRE(result.branch == "bearer_fail");
}

TEST_CASE("GatewayHttpAuthService rate limiter denial short-circuits bearer auth", "[gateway][http-auth][phase3]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";
	context.headers.insert_or_assign("Authorization", "Bearer will-be-limited");

	bool authorizeCalled = false;
	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.checkRateLimit = [](const GatewayHttpAuthRequestContext&, std::string& reasonOut) {
		reasonOut = "rate_limit_exceeded";
		return false;
		};
	callbacks.authorizeBearer = [&authorizeCalled](
		const std::string&,
		const GatewayHttpAuthRequestContext&,
		std::string&) {
			authorizeCalled = true;
			return true;
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE_FALSE(result.ok);
	REQUIRE(result.reason == "rate_limit_exceeded");
	REQUIRE(result.branch == "rate_limited");
	REQUIRE_FALSE(authorizeCalled);
}

TEST_CASE("GatewayHttpAuthService falls back to canvas capability when bearer missing", "[gateway][http-auth][phase4]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";
	context.canvasCapability = "cap-ok";

	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.authorizeCanvasCapability = [](
		const std::string& capability,
		const GatewayHttpAuthRequestContext&,
		std::string&) {
			return capability == "cap-ok";
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE(result.ok);
	REQUIRE(result.branch == "capability_ok");
}

TEST_CASE("GatewayHttpAuthService decision observer receives branch outcomes", "[gateway][http-auth][phase5]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";
	context.headers.insert_or_assign("Authorization", "Bearer token-obs");

	std::string observedBranch;
	GatewayHttpAuthPolicyCallbacks callbacks;
	callbacks.authorizeBearer = [](
		const std::string&,
		const GatewayHttpAuthRequestContext&,
		std::string&) {
			return true;
		};
	callbacks.observeDecision = [&observedBranch](
		const GatewayHttpAuthRequestContext&,
		const GatewayHttpAuthDecisionResult& result) {
			observedBranch = result.branch;
		};

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, callbacks);
	REQUIRE(result.ok);
	REQUIRE(result.branch == "bearer_ok");
	REQUIRE(observedBranch == "bearer_ok");
}

TEST_CASE("GatewayHttpAuthService no bearer token returns unauthorized", "[gateway][http-auth][phase3]") {
	GatewayHttpAuthService service;
	GatewayHttpAuthRequestContext context;
	context.path = "/__openclaw__/canvas";

	const GatewayHttpAuthDecisionResult result =
		service.AuthorizeCanvasRequest(context, GatewayHttpAuthPolicyCallbacks{});
	REQUIRE_FALSE(result.ok);
	REQUIRE(result.reason == "unauthorized");
	REQUIRE(result.branch == "unauthorized");
}
