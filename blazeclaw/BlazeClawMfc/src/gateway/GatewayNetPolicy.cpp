#include "pch.h"
#include "GatewayNetPolicy.h"

#include <WinSock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace blazeclaw::gateway {
	namespace {

		/// `isContainerEnvironment` one-shot cache; `ResetContainerEnvironmentCacheForTest` can clear.
		std::optional<bool> g_containerEnvironmentCache;

		static bool g_wsaNetPolicyInit = false;

		struct WsaUse {
			WsaUse() {
				if (!g_wsaNetPolicyInit) {
					WSADATA data{};
					(void)WSAStartup(MAKEWORD(2, 2), &data);
					g_wsaNetPolicyInit = true;
				}
			}
		};

		std::string ToLowerAscii(std::string value) {
			std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			});
			return value;
		}

		std::string ToLowerAsciiView(std::string_view value) {
			return ToLowerAscii(std::string(value));
		}

		std::string_view TrimView(std::string_view value) {
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
				value.remove_prefix(1);
			}
			while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
				value.remove_suffix(1);
			}
			return value;
		}

		void StripZoneSuffix(std::string& value) {
			const std::size_t pct = value.find('%');
			if (pct != std::string::npos) {
				value.resize(pct);
			}
		}

		bool Pton4(const std::string& s, IN_ADDR* out) {
			if (s.empty()) {
				return false;
			}
			return ::inet_pton(AF_INET, s.c_str(), out) == 1;
		}

		bool Pton6(std::string s, IN6_ADDR* out) {
			StripZoneSuffix(s);
			if (s.empty()) {
				return false;
			}
			return ::inet_pton(AF_INET6, s.c_str(), out) == 1;
		}

		int ClassifyIp(const std::string& sIn) {
			std::string s = sIn;
			StripZoneSuffix(s);
			IN_ADDR a4{};
			IN6_ADDR a6{};
			if (Pton4(s, &a4)) {
				return 4;
			}
			if (Pton6(s, &a6)) {
				return 6;
			}
			return 0;
		}

		bool IsIpv4LoopbackOctets(const std::uint8_t o0, const std::uint8_t o1, const std::uint8_t o2, const std::uint8_t o3) {
			(void)o1;
			(void)o2;
			(void)o3;
			return o0 == 127;
		}

		bool IsIpv4PrivateOrLoopbackOctets(
			const std::uint8_t o0,
			const std::uint8_t o1,
			const std::uint8_t o2,
			const std::uint8_t o3) {
			if (IsIpv4LoopbackOctets(o0, o1, o2, o3)) {
				return true;
			}
			if (o0 == 10) {
				return true;
			}
			if (o0 == 100 && o1 >= 64 && o1 <= 127) { // 100.64.0.0/10
				return true;
			}
			if (o0 == 172 && o1 >= 16 && o1 <= 31) {
				return true;
			}
			if (o0 == 192 && o1 == 168) {
				return true;
			}
			if (o0 == 169 && o1 == 254) {
				return true;
			}
			if (o0 == 0 && o1 == 0 && o2 == 0 && o3 == 0) {
				return true;
			}
			return false;
		}

		bool Ipv4MatchCidr(const IN_ADDR& ip, const IN_ADDR& base, int prefix) {
			if (prefix < 0 || prefix > 32) {
				return false;
			}
			if (prefix == 0) {
				return true;
			}
			const std::uint32_t a = ntohl(ip.s_addr);
			const std::uint32_t b = ntohl(base.s_addr);
			if (prefix == 32) {
				return a == b;
			}
			const std::uint32_t mask = 0xFFFFFFFFu << (32u - static_cast<std::uint32_t>(prefix));
			return (a & mask) == (b & mask);
		}

		bool Ipv6MatchCidr(const IN6_ADDR& ip, const IN6_ADDR& base, int prefix) {
			if (prefix < 0 || prefix > 128) {
				return false;
			}
			if (prefix == 0) {
				return true;
			}
			int rem = prefix;
			for (int i = 0; i < 16; ++i) {
				if (rem == 0) {
					return true;
				}
				const int take = (std::min)(8, rem);
				const unsigned char m =
					take == 8 ? 0xFFu : static_cast<unsigned char>(0xFFu << (8u - static_cast<unsigned>(take)));
				if ((ip.s6_addr[i] & m) != (base.s6_addr[i] & m)) {
					return false;
				}
				rem -= take;
			}
			return true;
		}

		std::string StripOptionalPort(std::string token) {
			if (token.empty()) {
				return token;
			}
			StripZoneSuffix(token);
			if (!token.empty() && token[0] == '[') {
				const std::size_t end = token.find(']');
				if (end != std::string::npos && end >= 1) {
					return token.substr(1, end - 1);
				}
			}
			{
				IN_ADDR t4{};
				IN6_ADDR t6{};
				if (::inet_pton(AF_INET, token.c_str(), &t4) == 1) {
					return token;
				}
				if (::inet_pton(AF_INET6, token.c_str(), &t6) == 1) {
					return token;
				}
			}
			const std::size_t lastColon = token.rfind(':');
			if (lastColon != std::string::npos && token.find('.') != std::string::npos &&
				token.find(':') == lastColon) {
				const std::string candidate = token.substr(0, lastColon);
				IN_ADDR t4b{};
				if (::inet_pton(AF_INET, candidate.c_str(), &t4b) == 1) {
					return candidate;
				}
			}
			return token;
		}

		std::optional<std::string> ParseIpToken(std::string_view raw) {
			std::string t = StripOptionalPort(std::string(TrimView(raw)));
			StripZoneSuffix(t);
			if (t.empty() || ClassifyIp(t) == 0) {
				return std::nullopt;
			}
			if (ClassifyIp(t) == 4) {
				IN_ADDR a4{};
				Pton4(t, &a4);
				char buf[NI_MAXHOST]{};
				if (::inet_ntop(AF_INET, &a4, buf, sizeof(buf)) == nullptr) {
					return std::nullopt;
				}
				return ToLowerAscii(std::string(buf));
			}
			IN6_ADDR a6{};
			if (!Pton6(t, &a6)) {
				return std::nullopt;
			}
			char buf[NI_MAXHOST]{};
			if (::inet_ntop(AF_INET6, &a6, buf, sizeof(buf)) == nullptr) {
				return std::nullopt;
			}
			return ToLowerAscii(std::string(buf));
		}

		bool V6PrivateOrLoopbackClass(const IN6_ADDR& a) {
			// fe80::/10
			if (a.s6_addr[0] == 0xFE && (a.s6_addr[1] & 0xC0) == 0x80) {
				return true;
			}
			// fc00::/7 (ULA)
			if ((a.s6_addr[0] & 0xFE) == 0xFC) {
				return true;
			}
			// ::1
			unsigned char l[16] = {};
			l[15] = 1;
			if (std::memcmp(a.s6_addr, l, 16) == 0) {
				return true;
			}
			// :: (unspecified)
			bool allZero = true;
			for (int i = 0; i < 16; ++i) {
				if (a.s6_addr[i] != 0) {
					allZero = false;
					break;
				}
			}
			if (allZero) {
				return true;
			}
			// ::ffff:0:0/96 + embedded IPv4 private/loopback
			if (a.s6_addr[0] == 0 && a.s6_addr[1] == 0 && a.s6_addr[2] == 0 && a.s6_addr[3] == 0 &&
				a.s6_addr[4] == 0 && a.s6_addr[5] == 0 && a.s6_addr[6] == 0 && a.s6_addr[7] == 0 &&
				a.s6_addr[8] == 0 && a.s6_addr[9] == 0 && a.s6_addr[10] == 0xff && a.s6_addr[11] == 0xff) {
				return IsIpv4PrivateOrLoopbackOctets(
					a.s6_addr[12],
					a.s6_addr[13],
					a.s6_addr[14],
					a.s6_addr[15]);
			}
			return false;
		}

		std::optional<std::string> ParseRealIp(const std::string& realIp) {
			return ParseIpToken(realIp);
		}

		bool DetectContainerOnce() {
#if defined(_WIN32)
			return false;
#else
			for (const char* const sentinel : {"/.dockerenv", "/run/.containerenv", "/var/run/.containerenv"}) {
				std::ifstream f(sentinel);
				if (f.good()) {
					return true;
				}
			}
			try {
				std::ifstream cg("/proc/1/cgroup");
				if (cg) {
					std::stringstream buffer;
					buffer << cg.rdbuf();
					const std::string text = buffer.str();
					if (text.find("/docker/") != std::string::npos ||
						text.find("cri-containerd-") != std::string::npos ||
						text.find("containerd/") != std::string::npos ||
						text.find("kubepods") != std::string::npos ||
						text.find("lxc") != std::string::npos) {
						return true;
					}
				}
			} catch (...) {
			}
#endif
			return false;
		}

		bool EndsWithDotTsNet(const std::string& lowerHost) {
			constexpr std::string_view kSuf = ".ts.net";
			if (lowerHost.size() < kSuf.size()) {
				return false;
			}
			return std::string_view(lowerHost)
				.compare(lowerHost.size() - kSuf.size(), kSuf.size(), kSuf) == 0;
		}

		struct ParsedHostForChecks {
			bool isLocalhost = false;
			std::string unbracketed;
		};

		std::optional<ParsedHostForChecks> TryParseHostForAddressChecks(const std::string& rawInput) {
			if (rawInput.empty()) {
				return std::nullopt;
			}
			std::string n = ToLowerAscii(rawInput);
			while (!n.empty() && n.back() == '.') {
				n.pop_back();
			}
			if (n == "localhost") {
				return ParsedHostForChecks{ .isLocalhost = true, .unbracketed = "localhost" };
			}
			if (!n.empty() && n[0] == '[') {
				const std::size_t end = n.find(']');
				if (end == std::string::npos) {
					return std::nullopt;
				}
				return ParsedHostForChecks{ .isLocalhost = false, .unbracketed = n.substr(1, end - 1) };
			}
			return ParsedHostForChecks{ .isLocalhost = false, .unbracketed = std::move(n) };
		}

		/// Strips :port, userinfo, handles bracketed v6 (OpenClaw URL hostname extraction intent).
		std::string ExtractHostFromUrlAuthority(std::string_view authority) {
			std::string a = std::string(TrimView(authority));
			{
				const std::size_t at = a.rfind('@');
				if (at != std::string::npos) {
					a = a.substr(at + 1);
				}
			}
			if (a.empty()) {
				return {};
			}
			if (a[0] == '[') {
				const std::size_t end = a.find(']');
				if (end == std::string::npos) {
					return {};
				}
				return a.substr(1, end - 1);
			}
			{
				const int cls = ClassifyIp(a);
				if (cls == 4) {
					return a;
				}
				if (cls == 6) {
					return a;
				}
			}
			const std::size_t lastColon = a.rfind(':');
			if (lastColon != std::string::npos && lastColon > 0) {
				const std::string_view portPart = std::string_view(a).substr(lastColon + 1);
				if (!portPart.empty()) {
					bool allDigits = true;
					for (const char c : portPart) {
						if (c < '0' || c > '9') {
							allDigits = false;
							break;
						}
					}
					if (allDigits) {
						return a.substr(0, lastColon);
					}
				}
			}
			return a;
		}

		bool V6UnspecifiedOrMulticastToExclude(const IN6_ADDR& a) {
			if (a.s6_addr[0] == 0xFF) {
				return true;
			}
			for (int i = 0; i < 16; ++i) {
				if (a.s6_addr[i] != 0) {
					return false;
				}
			}
			return true; // :: — excluded
		}

		/// `scheme://authority` without path/query/fragment; empty if not a `://` URL.
		std::string TakeUrlAuthorityString(std::string_view urlish) {
			const std::string s0 = std::string(TrimView(urlish));
			if (s0.empty()) {
				return {};
			}
			const std::size_t schemePos = s0.find("://");
			if (schemePos == std::string::npos) {
				return {};
			}
			std::string after = s0.substr(schemePos + 3);
			{
				const std::size_t d = after.find_first_of("/?#");
				if (d != std::string::npos) {
					after.resize(d);
				}
			}
			return after;
		}

	} // namespace

	std::string GatewayNetPolicy::NormalizeHostHeader(std::string_view hostHeader) {
		return ToLowerAsciiView(TrimView(hostHeader));
	}

	std::string GatewayNetPolicy::ResolveHostName(std::string_view hostHeader) {
		const std::string host = NormalizeHostHeader(hostHeader);
		if (host.empty()) {
			return {};
		}
		if (host[0] == '[') {
			const std::size_t end = host.find(']');
			if (end != std::string::npos) {
				return host.substr(1, end - 1);
			}
		}
		if (ClassifyIp(host) == 6) {
			// unbracketed v6, no port
			return host;
		}
		const std::size_t colon = host.find(':');
		if (colon == std::string::npos) {
			return host;
		}
		return host.substr(0, colon);
	}

	bool GatewayNetPolicy::IsLoopbackAddress(std::string_view ip) {
		const auto n = ParseIpToken(ip);
		if (!n) {
			return false;
		}
		const std::string& s = *n;
		IN_ADDR a4{};
		if (Pton4(s, &a4)) {
			const std::uint8_t* o = reinterpret_cast<const std::uint8_t*>(&a4);
			return IsIpv4LoopbackOctets(o[0], o[1], o[2], o[3]);
		}
		IN6_ADDR a6{};
		if (Pton6(s, &a6)) {
			unsigned char l[16] = {};
			l[15] = 1;
			if (std::memcmp(a6.s6_addr, l, 16) == 0) {
				return true;
			}
			if (a6.s6_addr[0] == 0 && a6.s6_addr[1] == 0 && a6.s6_addr[2] == 0 && a6.s6_addr[3] == 0 &&
				a6.s6_addr[4] == 0 && a6.s6_addr[5] == 0 && a6.s6_addr[6] == 0 && a6.s6_addr[7] == 0 &&
				a6.s6_addr[8] == 0 && a6.s6_addr[9] == 0 && a6.s6_addr[10] == 0xff && a6.s6_addr[11] == 0xff) {
				return a6.s6_addr[12] == 127;
			}
		}
		return false;
	}

	bool GatewayNetPolicy::IsLocalGatewayAddress(
		std::string_view ip,
		const IsLocalGatewayAddressOptions& options) {
		if (TrimView(ip).empty()) {
			return false;
		}
		if (IsLoopbackAddress(ip)) {
			return true;
		}
		const auto n = ParseIpToken(ip);
		if (!n) {
			return false;
		}
		if (options.primaryTailnetIpv4.has_value() && !options.primaryTailnetIpv4->empty()) {
			if (const auto t4 = ParseIpToken(*options.primaryTailnetIpv4)) {
				if (*n == *t4) {
					return true;
				}
			}
		}
		if (options.primaryTailnetIpv6.has_value() && !options.primaryTailnetIpv6->empty()) {
			const std::string a = ToLowerAscii(std::string(TrimView(ip)));
			const std::string b = ToLowerAscii(std::string(TrimView(*options.primaryTailnetIpv6)));
			if (a == b) {
				return true;
			}
		}
		return false;
	}

	bool GatewayNetPolicy::IsPrivateOrLoopbackAddress(std::string_view ip) {
		const auto n = ParseIpToken(ip);
		if (!n) {
			return false;
		}
		const std::string& s = *n;
		IN_ADDR a4{};
		if (Pton4(s, &a4)) {
			const std::uint8_t* o = reinterpret_cast<const std::uint8_t*>(&a4);
			return IsIpv4PrivateOrLoopbackOctets(o[0], o[1], o[2], o[3]);
		}
		IN6_ADDR a6{};
		if (Pton6(s, &a6)) {
			return V6PrivateOrLoopbackClass(a6);
		}
		return false;
	}

	bool GatewayNetPolicy::IsIpInCidr(std::string_view ip, std::string_view cidrOrHost) {
		const auto n = ParseIpToken(ip);
		if (!n) {
			return false;
		}
		const std::string& ipS = *n;
		const std::string cidr = std::string(TrimView(cidrOrHost));
		if (cidr.empty()) {
			return false;
		}
		if (cidr.find('/') == std::string::npos) {
			const auto ex = ParseIpToken(cidr);
			if (!ex) {
				return false;
			}
			return *n == *ex;
		}
		const std::size_t slash = cidr.find('/');
		if (slash == std::string::npos) {
			return false;
		}
		const std::string baseStr = cidr.substr(0, slash);
		const int prefix = std::atoi(cidr.c_str() + slash + 1);
		IN_ADDR b4{};
		IN6_ADDR b6{};
		IN_ADDR i4{};
		IN6_ADDR i6{};
		if (Pton4(baseStr, &b4) && Pton4(ipS, &i4)) {
			return Ipv4MatchCidr(i4, b4, prefix);
		}
		if (Pton6(baseStr, &b6) && Pton6(std::string(ipS), &i6)) {
			return Ipv6MatchCidr(i6, b6, prefix);
		}
		return false;
	}

	bool GatewayNetPolicy::IsTrustedProxyAddress(
		std::string_view ip,
		const std::vector<std::string>& trustedProxies) {
		const auto n = ParseIpToken(ip);
		if (!n || trustedProxies.empty()) {
			return false;
		}
		for (const auto& proxy : trustedProxies) {
			const std::string c = std::string(TrimView(proxy));
			if (c.empty()) {
				continue;
			}
			if (IsIpInCidr(*n, c)) {
				return true;
			}
		}
		return false;
	}

	std::optional<std::string> GatewayNetPolicy::ResolveClientIp(const ResolveClientIpParams& p) {
		const auto remoteNorm = ParseIpToken(p.remoteAddr);
		if (!remoteNorm) {
			return std::nullopt;
		}
		const std::vector<std::string> emptyList;
		const std::vector<std::string>& trust =
			p.trustedProxies != nullptr ? *p.trustedProxies : emptyList;
		if (!IsTrustedProxyAddress(*remoteNorm, trust)) {
			return remoteNorm;
		}
		std::optional<std::string> forwarded;
		if (!trust.empty()) {
			const std::string chain = p.forwardedFor;
			if (!chain.empty()) {
				std::vector<std::string> hops;
				std::string acc;
				for (const char c : chain) {
					if (c == ',') {
						if (const auto t = ParseIpToken(acc)) {
							hops.push_back(*t);
						}
						acc.clear();
					}
					else {
						acc.push_back(c);
					}
				}
				if (const auto t = ParseIpToken(acc)) {
					hops.push_back(*t);
				}
				if (!hops.empty()) {
					for (int i = static_cast<int>(hops.size()) - 1; i >= 0; --i) {
						const std::string& hop = hops[static_cast<std::size_t>(i)];
						if (IsLoopbackAddress(hop)) {
							continue;
						}
						if (!IsTrustedProxyAddress(hop, trust)) {
							forwarded = hop;
							break;
						}
					}
				}
			}
		}
		if (forwarded.has_value()) {
			return forwarded;
		}
		if (p.allowRealIpFallback) {
			return ParseRealIp(p.realIp);
		}
		return std::nullopt;
	}

	bool GatewayNetPolicy::IsContainerEnvironment() noexcept {
		if (!g_containerEnvironmentCache.has_value()) {
			g_containerEnvironmentCache = DetectContainerOnce();
		}
		return *g_containerEnvironmentCache;
	}

	void GatewayNetPolicy::ResetContainerEnvironmentCacheForTest() noexcept {
		g_containerEnvironmentCache.reset();
	}

	bool GatewayNetPolicy::IsValidIPv4(std::string_view host) {
		const std::string s(TrimView(host));
		if (s.empty() || s.find('.') == std::string::npos) {
			return false;
		}
		// Reject non-canonical (no leading-zero except single 0) — align with 4-decimal form.
		std::vector<std::string> parts;
		std::string cur;
		for (const char c : s) {
			if (c == '.') {
				parts.push_back(cur);
				cur.clear();
			}
			else {
				cur.push_back(c);
			}
		}
		parts.push_back(cur);
		if (parts.size() != 4) {
			return false;
		}
		for (const auto& p : parts) {
			if (p.empty() || p.size() > 3) {
				return false;
			}
			if (p.size() > 1 && p[0] == '0') {
				return false;
			}
			for (const char c : p) {
				if (c < '0' || c > '9') {
					return false;
				}
			}
			const int v = std::atoi(p.c_str());
			if (v < 0 || v > 255) {
				return false;
			}
		}
		IN_ADDR a4{};
		return Pton4(s, &a4);
	}

	bool GatewayNetPolicy::IsPlausibleBindAddressString(std::string_view bindAddress) {
		if (bindAddress.empty()) {
			return false;
		}
		for (const char ch : bindAddress) {
			const bool ok = std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == '-' || ch == ':';
			if (!ok) {
				return false;
			}
		}
		return true;
	}

	std::vector<std::string> GatewayNetPolicy::BuildDefaultTrustedProxyCidrs() {
		return { "127.0.0.1", "::1" };
	}

	GatewayNetPolicy::BindMode GatewayNetPolicy::DefaultGatewayBindMode(
		const std::optional<std::string>& tailscaleMode) noexcept {
		if (tailscaleMode.has_value()) {
			const std::string t = ToLowerAscii(tailscaleMode.value());
			if (!t.empty() && t != "off") {
				return BindMode::Loopback;
			}
		}
		return IsContainerEnvironment() ? BindMode::Auto : BindMode::Loopback;
	}

	std::string GatewayNetPolicy::ResolveGatewayBindHost(
		BindMode mode,
		const std::optional<std::string>& customHost,
		const std::function<std::string()>& tryPickPrimaryTailnetIpv4,
		const std::function<bool(const std::string& host)>& canBindToHost,
		const bool isContainer) {
		const auto canBind = [&](const char* h) {
			return canBindToHost(std::string(h));
		};

		if (mode == BindMode::Loopback) {
			if (canBind("127.0.0.1")) {
				return "127.0.0.1";
			}
			return "0.0.0.0";
		}
		if (mode == BindMode::Tailnet) {
			if (tryPickPrimaryTailnetIpv4) {
				const std::string tail = tryPickPrimaryTailnetIpv4();
				if (!tail.empty() && canBind(tail.c_str())) {
					return tail;
				}
			}
			if (canBind("127.0.0.1")) {
				return "127.0.0.1";
			}
			return "0.0.0.0";
		}
		if (mode == BindMode::Lan) {
			return "0.0.0.0";
		}
		if (mode == BindMode::Custom) {
			if (!customHost.has_value()) {
				return "0.0.0.0";
			}
			const std::string h = ToLowerAsciiView(TrimView(customHost.value()));
			if (h.empty()) {
				return "0.0.0.0";
			}
			if (IsValidIPv4(h) && canBind(h.c_str())) {
				return h;
			}
			return "0.0.0.0";
		}
		if (mode == BindMode::Auto) {
			if (isContainer) {
				return "0.0.0.0";
			}
			if (canBind("127.0.0.1")) {
				return "127.0.0.1";
			}
			return "0.0.0.0";
		}
		return "0.0.0.0";
	}

	bool GatewayNetPolicy::CanBindToHost(const std::string& host) noexcept {
		WsaUse wsa;
		(void)wsa;
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_NUMERICHOST;
		addrinfo* res = nullptr;
		if (getaddrinfo(host.c_str(), "0", &hints, &res) != 0 || res == nullptr) {
			if (res) {
				freeaddrinfo(res);
			}
			return false;
		}
		bool ok = false;
		for (addrinfo* c = res; c != nullptr; c = c->ai_next) {
			const SOCKET s = ::socket(c->ai_family, c->ai_socktype, c->ai_protocol);
			if (s == INVALID_SOCKET) {
				continue;
			}
			{
				BOOL one = 1;
				(void)setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
			}
			if (c->ai_family == AF_INET6) {
				DWORD v6only = 0;
				(void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
			}
			if (bind(s, c->ai_addr, static_cast<int>(c->ai_addrlen)) != SOCKET_ERROR) {
				(void)closesocket(s);
				ok = true;
				break;
			}
			(void)closesocket(s);
		}
		freeaddrinfo(res);
		return ok;
	}

	bool GatewayNetPolicy::IsLoopbackHost(std::string_view host) {
		const std::string raw(host);
		const auto parsed = TryParseHostForAddressChecks(raw);
		if (!parsed) {
			return false;
		}
		if (parsed->isLocalhost) {
			return true;
		}
		return IsLoopbackAddress(parsed->unbracketed);
	}

	bool GatewayNetPolicy::IsPrivateOrLoopbackHost(std::string_view host) {
		const std::string raw(host);
		const auto parsed = TryParseHostForAddressChecks(raw);
		if (!parsed) {
			return false;
		}
		if (parsed->isLocalhost) {
			return true;
		}
		const auto n = ParseIpToken(parsed->unbracketed);
		if (!n) {
			return false;
		}
		if (!IsPrivateOrLoopbackAddress(*n)) {
			return false;
		}
		const std::string& s = *n;
		IN6_ADDR a6{};
		if (Pton6(s, &a6) && V6UnspecifiedOrMulticastToExclude(a6)) {
			return false;
		}
		return true;
	}

	bool GatewayNetPolicy::IsLocalishHost(std::string_view hostHeader) {
		if (hostHeader.empty()) {
			return false;
		}
		const std::string host = ResolveHostName(NormalizeHostHeader(hostHeader));
		if (host.empty()) {
			return false;
		}
		if (IsLoopbackHost(host)) {
			return true;
		}
		return EndsWithDotTsNet(host);
	}

	bool GatewayNetPolicy::IsLocalishHttpOrigin(const std::string_view origin) {
		if (origin.empty()) {
			return true;
		}
		const std::string s0 = std::string(TrimView(origin));
		if (s0.empty()) {
			return true;
		}
		const std::size_t schemePos = s0.find("://");
		if (schemePos == std::string::npos) {
			return false;
		}
		const std::string lowerScheme = ToLowerAscii(s0.substr(0, schemePos));
		if (lowerScheme != "http" && lowerScheme != "https") {
			return false;
		}
		const std::string after = TakeUrlAuthorityString(s0);
		if (after.empty()) {
			return false;
		}
		return IsLocalishHost(after);
	}

	bool GatewayNetPolicy::IsSecureWebSocketUrl(
		const std::string_view url,
		const IsSecureWebSocketUrlOptions& options) {
		const std::string s0 = std::string(TrimView(url));
		if (s0.empty()) {
			return false;
		}
		const std::size_t schemePos = s0.find("://");
		if (schemePos == std::string::npos) {
			return false;
		}
		const std::string lowerScheme = ToLowerAscii(s0.substr(0, schemePos));
		if (lowerScheme != "ws" && lowerScheme != "wss" && lowerScheme != "http" && lowerScheme != "https") {
			return false;
		}
		const std::string after = TakeUrlAuthorityString(s0);
		const std::string host = ExtractHostFromUrlAuthority(after);
		if (host.empty()) {
			return false;
		}
		if (lowerScheme == "wss" || lowerScheme == "https") {
			return true;
		}
		// effective ws
		if (IsLoopbackHost(host)) {
			return true;
		}
		if (options.allowPrivateWs) {
			if (IsPrivateOrLoopbackHost(host)) {
				return true;
			}
			std::string h = host;
			StripZoneSuffix(h);
			if (h.size() >= 2 && h[0] == '[' && h.back() == ']') {
				h = h.substr(1, h.size() - 2);
			}
			StripZoneSuffix(h);
			return ClassifyIp(h) == 0;
		}
		return false;
	}

	std::vector<std::string> GatewayNetPolicy::ResolveGatewayListenHosts(
		const std::string& bindHost,
		const std::function<bool(const std::string& host)>& canBindToHost) {
		const auto can = canBindToHost
			? canBindToHost
			: std::function<bool(const std::string& h)>([](const std::string& h) {
			return CanBindToHost(h);
		});
		if (bindHost != "127.0.0.1") {
			return { bindHost };
		}
		if (can("::1")) {
			return { "127.0.0.1", "::1" };
		}
		return { bindHost };
	}

} // namespace blazeclaw::gateway
