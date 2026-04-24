#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blazeclaw::gateway {

	/// OpenClaw `openclaw/src/gateway/net.ts` parity — centralized host/IP policy helpers.
	struct GatewayNetPolicy {
		[[nodiscard]] static std::string NormalizeHostHeader(std::string_view hostHeader);

		/// Host / IP portion only (strips :port, handles bracketed IPv6).
		[[nodiscard]] static std::string ResolveHostName(std::string_view hostHeader);

		[[nodiscard]] static bool IsLoopbackAddress(std::string_view ip);

		/// RFC1918, CGNAT, link-local, ULA, loopback (loose match vs OpenClaw `ip` ranges).
		[[nodiscard]] static bool IsPrivateOrLoopbackAddress(std::string_view ip);

		/// OpenClaw `isLocalGatewayAddress` — loopback, or match optional primary Tailnet IPs
		/// (callers pass values from host introspection; there is no `pickPrimaryTailnet*` in this module).
		struct IsLocalGatewayAddressOptions {
			std::optional<std::string> primaryTailnetIpv4;
			std::optional<std::string> primaryTailnetIpv6;
		};

		[[nodiscard]] static bool IsLocalGatewayAddress(
			std::string_view ip,
			const IsLocalGatewayAddressOptions& options = {});

		/// `cidr` may be an exact host/IP or a `prefix/len` (IPv4 or IPv6).
		[[nodiscard]] static bool IsIpInCidr(std::string_view ip, std::string_view cidr);

		[[nodiscard]] static bool IsTrustedProxyAddress(
			std::string_view ip,
			const std::vector<std::string>& trustedProxies);

		struct ResolveClientIpParams {
			std::string remoteAddr;
			std::string forwardedFor;
			std::string realIp;
			const std::vector<std::string>* trustedProxies = nullptr;
			bool allowRealIpFallback = false;
		};

		/// OpenClaw fail-closed semantics: returns nullopt if remote is a trusted hop but
		/// a client-origin IP cannot be derived and X-Real-IP is not allowed/missing.
		[[nodiscard]] static std::optional<std::string> ResolveClientIp(const ResolveClientIpParams& params);

		[[nodiscard]] static bool IsContainerEnvironment() noexcept;

		/// OpenClaw `__resetContainerCacheForTest` — clears cached `IsContainerEnvironment` result.
		static void ResetContainerEnvironmentCacheForTest() noexcept;

		/// Dotted-decimal IPv4 only (OpenClaw `isValidIPv4` / `isCanonicalDottedDecimalIPv4` intent).
		[[nodiscard]] static bool IsValidIPv4(std::string_view host);

		/// Charset validation for configured bind host strings (legacy `IsPlausibleBindAddress` surface).
		[[nodiscard]] static bool IsPlausibleBindAddressString(std::string_view bindAddress);

		[[nodiscard]] static std::vector<std::string> BuildDefaultTrustedProxyCidrs();

		enum class BindMode {
			Loopback,
			Lan,
			Tailnet,
			Auto,
			Custom,
		};

		/// Optional tailnet discoverer — when empty/invalid, `Tailnet` falls back like OpenClaw.
		[[nodiscard]] static std::string ResolveGatewayBindHost(
			BindMode mode,
			const std::optional<std::string>& customHost,
			const std::function<std::string()>& tryPickPrimaryTailnetIpv4,
			const std::function<bool(const std::string& host)>& canBindToHost,
			bool isContainer);

		/// `tailscaleMode` non-empty and not "off" forces `loopback` in OpenClaw; otherwise auto vs loopback.
		[[nodiscard]] static BindMode DefaultGatewayBindMode(
			const std::optional<std::string>& tailscaleMode) noexcept;

		[[nodiscard]] static bool CanBindToHost(const std::string& host) noexcept;

		/// `net.ts` `isLoopbackHost` (localhost, 127.x, ::1, ::ffff:127.x forms via IP parse).
		[[nodiscard]] static bool IsLoopbackHost(std::string_view host);

		/// `net.ts` `isPrivateOrLoopbackHost` (excludes `::` and IPv6 multicast for v6).
		[[nodiscard]] static bool IsPrivateOrLoopbackHost(std::string_view host);

		/// `net.ts` `isLocalishHost` — loopback + `*.ts.net` (Tailscale Serve/Funnel class).
		[[nodiscard]] static bool IsLocalishHost(std::string_view hostHeader);

		/// Browser `Origin` value (`http(s)://host[:port]`, optional path) using `isLocalishHost` on the authority.
		/// Empty origin is allowed (e.g. non-browser client).
		[[nodiscard]] static bool IsLocalishHttpOrigin(std::string_view origin);

		struct IsSecureWebSocketUrlOptions {
			/// Aligned with OpenClaw `isSecureWebSocketUrl` / break-glass private `ws://`.
			bool allowPrivateWs = false;
		};

		/// `net.ts` `isSecureWebSocketUrl` (ws/wss/http/https aliasing like Node).
		[[nodiscard]] static bool IsSecureWebSocketUrl(
			std::string_view url,
			const IsSecureWebSocketUrlOptions& options = {});

		/// `net.ts` `resolveGatewayListenHosts` — when `127.0.0.1` and `::1` can bind, return both.
		[[nodiscard]] static std::vector<std::string> ResolveGatewayListenHosts(
			const std::string& bindHost,
			const std::function<bool(const std::string& host)>& canBindToHost = {});
	};

} // namespace blazeclaw::gateway
