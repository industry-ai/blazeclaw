#include "gateway/GatewayNetPolicy.h"

#include <catch2/catch_all.hpp>
#include <optional>
#include <string>
#include <vector>

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

TEST_CASE("GatewayNetPolicy N4: isLoopbackHost / isLocalishHost / isPrivateOrLoopbackHost (net.ts)", "[gateway][net][n4]") {
	REQUIRE(GatewayNetPolicy::IsLoopbackHost("localhost"));
	REQUIRE(GatewayNetPolicy::IsLoopbackHost("127.0.0.1"));
	REQUIRE(GatewayNetPolicy::IsLoopbackHost("[::1]"));
	REQUIRE(GatewayNetPolicy::IsLocalishHost("myhost.ts.net"));
	REQUIRE(GatewayNetPolicy::IsLocalishHost("[::1]:9000"));
	REQUIRE(GatewayNetPolicy::IsPrivateOrLoopbackHost("10.0.0.1"));
	REQUIRE_FALSE(GatewayNetPolicy::IsPrivateOrLoopbackHost("203.0.113.1"));
}

TEST_CASE("GatewayNetPolicy N4: isSecureWebSocketUrl (net.ts)", "[gateway][net][n4]") {
	REQUIRE(GatewayNetPolicy::IsSecureWebSocketUrl("wss://127.0.0.1/x"));
	REQUIRE(GatewayNetPolicy::IsSecureWebSocketUrl("https://127.0.0.1/"));
	REQUIRE(GatewayNetPolicy::IsSecureWebSocketUrl("ws://127.0.0.1/"));
	REQUIRE(GatewayNetPolicy::IsSecureWebSocketUrl("http://[::1]/"));
	REQUIRE_FALSE(GatewayNetPolicy::IsSecureWebSocketUrl("ws://8.8.8.8/"));
}

TEST_CASE("GatewayNetPolicy N4: isLocalishHttpOrigin (browser Origin vs isLocalishHost)", "[gateway][net][n4]") {
	REQUIRE(GatewayNetPolicy::IsLocalishHttpOrigin(""));
	REQUIRE(GatewayNetPolicy::IsLocalishHttpOrigin("http://127.0.0.1:8080"));
	REQUIRE(GatewayNetPolicy::IsLocalishHttpOrigin("https://[::1]:9000/"));
	REQUIRE(GatewayNetPolicy::IsLocalishHttpOrigin("https://foo.ts.net/"));
	REQUIRE_FALSE(GatewayNetPolicy::IsLocalishHttpOrigin("https://203.0.113.1/"));
	REQUIRE_FALSE(GatewayNetPolicy::IsLocalishHttpOrigin("file:///x"));
}

TEST_CASE("GatewayNetPolicy N4: resolveGatewayListenHosts (net.ts)", "[gateway][net][n4]") {
	const std::vector<std::string> nonLocal = GatewayNetPolicy::ResolveGatewayListenHosts("0.0.0.0", {});
	REQUIRE(nonLocal.size() == 1);
	REQUIRE(nonLocal[0] == "0.0.0.0");
	const std::vector<std::string> loop = GatewayNetPolicy::ResolveGatewayListenHosts("127.0.0.1", {});
	REQUIRE(!loop.empty());
	REQUIRE(loop[0] == "127.0.0.1");
}

TEST_CASE("GatewayNetPolicy: isLocalGatewayAddress (net.ts) + container cache test hook", "[gateway][net]") {
	REQUIRE(GatewayNetPolicy::IsLocalGatewayAddress("127.0.0.1"));
	REQUIRE(GatewayNetPolicy::IsLocalGatewayAddress("::1"));
	REQUIRE_FALSE(GatewayNetPolicy::IsLocalGatewayAddress("8.8.8.8"));
	const bool before = GatewayNetPolicy::IsContainerEnvironment();
	(void)before;
	GatewayNetPolicy::ResetContainerEnvironmentCacheForTest();
	REQUIRE(GatewayNetPolicy::IsContainerEnvironment() == before);

	{
		GatewayNetPolicy::IsLocalGatewayAddressOptions opt;
		opt.primaryTailnetIpv4 = std::string("100.64.0.7");
		REQUIRE(GatewayNetPolicy::IsLocalGatewayAddress("100.64.0.7", opt));
	}
	{
		GatewayNetPolicy::IsLocalGatewayAddressOptions opt;
		opt.primaryTailnetIpv4 = std::string("100.64.0.7");
		REQUIRE_FALSE(GatewayNetPolicy::IsLocalGatewayAddress("10.0.0.1", opt));
	}
	{
		GatewayNetPolicy::IsLocalGatewayAddressOptions opt;
		opt.primaryTailnetIpv6 = std::string("fd00::1");
		REQUIRE(GatewayNetPolicy::IsLocalGatewayAddress("fd00::1", opt));
	}
	{
		GatewayNetPolicy::IsLocalGatewayAddressOptions opt;
		opt.primaryTailnetIpv6 = std::string("fd00::1");
		REQUIRE(GatewayNetPolicy::IsLocalGatewayAddress("FD00::1", opt));
	}
}
