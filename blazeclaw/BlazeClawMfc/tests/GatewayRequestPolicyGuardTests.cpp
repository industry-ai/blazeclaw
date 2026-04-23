#include "gateway/GatewayRequestPolicyGuard.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::GatewayRequestPolicyGuard;
using blazeclaw::gateway::protocol::RequestFrame;

namespace {
	RequestFrame MakeRequest(
		const std::string& id,
		const std::string& method,
		const std::optional<std::string>& paramsJson = std::nullopt) {
		RequestFrame request;
		request.id = id;
		request.method = method;
		request.paramsJson = paramsJson;
		return request;
	}
}

TEST_CASE("GatewayRequestPolicyGuard startup-gates OpenClaw control-plane writes with retryable error", "[gateway][policy][server-methods][s2]") {
	GatewayRequestPolicyGuard guard;
	const GatewayRequestPolicyGuard::Context context{
		.dispatchInitialized = true,
		.hostRunning = false,
	};

	for (const auto& method : { "config.apply", "config.patch", "update.run" }) {
		const auto error = guard.Evaluate(MakeRequest("startup-gate-" + std::string(method), method), context);
		REQUIRE(error.has_value());
		REQUIRE(error->code == "method_unavailable");
		REQUIRE(error->retryable.has_value());
		REQUIRE(error->retryable.value());
		REQUIRE(error->detailsJson.has_value());
		REQUIRE(error->detailsJson.value().find("startup_gated") != std::string::npos);
	}
}

TEST_CASE("GatewayRequestPolicyGuard transport control-plane methods enforce scope", "[gateway][policy][server-methods][s2]") {
	GatewayRequestPolicyGuard guard;
	const GatewayRequestPolicyGuard::Context context{
		.dispatchInitialized = true,
		.hostRunning = true,
	};

	const auto denied = guard.Evaluate(
		MakeRequest(
			"transport-scope-denied",
			"gateway.transport.policy.set",
			std::string("{\"scope\":\"runtime\"}")),
		context);
	REQUIRE(denied.has_value());
	REQUIRE(denied->code == "insufficient_scope");
	REQUIRE(denied->detailsJson.has_value());
	REQUIRE(denied->detailsJson.value().find("transport_scope_required") != std::string::npos);

	const auto allowedTransport = guard.Evaluate(
		MakeRequest(
			"transport-scope-allowed",
			"gateway.transport.policy.set",
			std::string("{\"scope\":\"transport\"}")),
		context);
	REQUIRE_FALSE(allowedTransport.has_value());

	const auto allowedAdmin = guard.Evaluate(
		MakeRequest(
			"transport-scope-admin",
			"gateway.transport.policy.set",
			std::string("{\"scope\":\"admin\"}")),
		context);
	REQUIRE_FALSE(allowedAdmin.has_value());
}

TEST_CASE("GatewayRequestPolicyGuard chat.writeBudget over limit returns rate_limited", "[gateway][policy][server-methods][s2]") {
	GatewayRequestPolicyGuard guard;
	const GatewayRequestPolicyGuard::Context context{
		.dispatchInitialized = true,
		.hostRunning = true,
	};

	const auto denied = guard.Evaluate(
		MakeRequest(
			"chat-budget-denied",
			"chat.send",
			std::string("{\"scope\":\"chat\",\"writeBudget\":129}")),
		context);
	REQUIRE(denied.has_value());
	REQUIRE(denied->code == "rate_limited");
	REQUIRE(denied->retryable.has_value());
	REQUIRE_FALSE(denied->retryable.value());
	REQUIRE(denied->detailsJson.has_value());
	REQUIRE(denied->detailsJson.value().find("write_budget_exceeded") != std::string::npos);

	const auto allowed = guard.Evaluate(
		MakeRequest(
			"chat-budget-allowed",
			"chat.send",
			std::string("{\"scope\":\"chat\",\"writeBudget\":128}")),
		context);
	REQUIRE_FALSE(allowed.has_value());
}
