#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace blazeclaw::gateway {

	struct GatewayHttpAuthRequestContext {
		std::string path;
		std::unordered_map<std::string, std::string> headers;
		std::string remoteIp;
		std::vector<std::string> trustedProxies;
		bool allowRealIpFallback = false;
		bool malformedScopedPath = false;
		std::string browserOriginPolicy;
		std::string canvasCapability;
	};

	struct GatewayHttpAuthDecisionResult {
		bool ok = false;
		std::string reason = "unauthorized";
		std::string branch;
	};

	using GatewayHttpAuthorizeBearerCallback = std::function<bool(
		const std::string& token,
		const GatewayHttpAuthRequestContext& context,
		std::string& failureReasonOut)>;

	using GatewayHttpAuthRateLimitCallback = std::function<bool(
		const GatewayHttpAuthRequestContext& context,
		std::string& failureReasonOut)>;

	using GatewayHttpAuthorizeCanvasCapabilityCallback = std::function<bool(
		const std::string& canvasCapability,
		const GatewayHttpAuthRequestContext& context,
		std::string& failureReasonOut)>;

	using GatewayHttpAuthDecisionObserver = std::function<void(
		const GatewayHttpAuthRequestContext& context,
		const GatewayHttpAuthDecisionResult& result)>;

	struct GatewayHttpAuthPolicyCallbacks {
		GatewayHttpAuthorizeBearerCallback authorizeBearer;
		GatewayHttpAuthRateLimitCallback checkRateLimit;
		GatewayHttpAuthorizeCanvasCapabilityCallback authorizeCanvasCapability;
		GatewayHttpAuthDecisionObserver observeDecision;
	};

	class GatewayHttpAuthService {
	public:
		[[nodiscard]] static bool IsCanvasPath(std::string_view pathname);
		[[nodiscard]] static bool IsMalformedScopedCanvasPath(std::string_view pathname);

		[[nodiscard]] static std::optional<std::string> GetBearerToken(
			const std::unordered_map<std::string, std::string>& headers);

		[[nodiscard]] GatewayHttpAuthDecisionResult AuthorizeCanvasRequest(
			const GatewayHttpAuthRequestContext& context,
			const GatewayHttpAuthPolicyCallbacks& callbacks) const;
	};

} // namespace blazeclaw::gateway
