#include "gateway/GatewayNetPolicy.h"

#include <catch2/catch_all.hpp>
#include <optional>
#include <string>

using blazeclaw::gateway::GatewayNetPolicy;

TEST_CASE("GatewayNetPolicy N1: host header normalization (OpenClaw net.ts)", "[gateway][net][n1]") {
	REQUIRE(GatewayNetPolicy::NormalizeHostHeader("  EXAMPLE.com ") == "example.com");
	REQUIRE(GatewayNetPolicy::ResolveHostName("[::1]:8080") == "::1");
	REQUIRE(GatewayNetPolicy::ResolveHostName("127.0.0.1:56789") == "127.0.0.1");
}

TEST_CASE("GatewayNetPolicy N1: loopback and private classifiers", "[gateway][net][n1]") {
	REQUIRE(GatewayNetPolicy::IsLoopbackAddress("127.0.0.1"));
	REQUIRE(GatewayNetPolicy::IsLoopbackAddress("::1"));
	REQUIRE_FALSE(GatewayNetPolicy::IsLoopbackAddress("8.8.8.8"));
	REQUIRE(GatewayNetPolicy::IsPrivateOrLoopbackAddress("10.0.0.1"));
	REQUIRE(GatewayNetPolicy::IsPrivateOrLoopbackAddress("192.168.0.1"));
}

TEST_CASE("GatewayNetPolicy N1: CIDR and exact trusted proxy match (OpenClaw isIpInCidr)", "[gateway][net][n1]") {
	const std::vector<std::string> list = { "10.0.0.0/8" };
	REQUIRE(GatewayNetPolicy::IsIpInCidr("10.5.0.1", "10.0.0.0/8"));
	REQUIRE(GatewayNetPolicy::IsTrustedProxyAddress("10.0.0.1", list));
}

TEST_CASE("GatewayNetPolicy N2: direct remote when not behind trusted hop", "[gateway][net][n2]") {
	const std::vector<std::string> trust = { "127.0.0.1" };
	const auto ip = GatewayNetPolicy::ResolveClientIp(GatewayNetPolicy::ResolveClientIpParams{
		.remoteAddr = "10.0.0.1",
		.trustedProxies = &trust,
	});
	REQUIRE(ip.has_value());
	REQUIRE(*ip == "10.0.0.1");
}

TEST_CASE("GatewayNetPolicy N2: X-Forwarded-For untrusted hop and fail-closed trusted edge", "[gateway][net][n2]") {
	const std::vector<std::string> trust = { "127.0.0.1" };
	{
		const auto ip = GatewayNetPolicy::ResolveClientIp(GatewayNetPolicy::ResolveClientIpParams{
			.remoteAddr = "127.0.0.1",
			.forwardedFor = "10.0.0.1, 203.0.113.9",
			.trustedProxies = &trust,
		});
		REQUIRE(ip.has_value());
		REQUIRE(*ip == "203.0.113.9");
	}
	{
		const auto ip = GatewayNetPolicy::ResolveClientIp(GatewayNetPolicy::ResolveClientIpParams{
			.remoteAddr = "127.0.0.1",
			.forwardedFor = "",
			.trustedProxies = &trust,
			.allowRealIpFallback = false,
		});
		REQUIRE_FALSE(ip.has_value());
	}
	{
		const auto ip = GatewayNetPolicy::ResolveClientIp(GatewayNetPolicy::ResolveClientIpParams{
			.remoteAddr = "127.0.0.1",
			.forwardedFor = "",
			.realIp = "198.51.100.1",
			.trustedProxies = &trust,
			.allowRealIpFallback = true,
		});
		REQUIRE(ip.has_value());
		REQUIRE(*ip == "198.51.100.1");
	}
}

TEST_CASE("GatewayNetPolicy N3: default bind mode tailscale + container branch", "[gateway][net][n3]") {
	const auto a = GatewayNetPolicy::DefaultGatewayBindMode(std::optional<std::string>{"funnel"});
	REQUIRE(a == GatewayNetPolicy::BindMode::Loopback);
	(void)GatewayNetPolicy::IsContainerEnvironment();
	(void)GatewayNetPolicy::DefaultGatewayBindMode(std::nullopt);
}

TEST_CASE("GatewayNetPolicy: IPv4 strict validator (OpenClaw isValidIPv4 class)", "[gateway][net]") {
	REQUIRE(GatewayNetPolicy::IsValidIPv4("192.168.0.1"));
	REQUIRE_FALSE(GatewayNetPolicy::IsValidIPv4("01.1.1.1"));
}
