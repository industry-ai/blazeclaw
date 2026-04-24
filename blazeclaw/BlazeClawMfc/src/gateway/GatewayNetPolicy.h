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
	};

} // namespace blazeclaw::gateway
