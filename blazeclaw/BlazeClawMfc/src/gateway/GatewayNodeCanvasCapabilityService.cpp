#include "pch.h"
#include "GatewayNodeCanvasCapabilityService.h"

#include "GatewayHostProtocolHelpers.h"

#include <cctype>

namespace blazeclaw::gateway {
	namespace {

		std::string TrimCopy(const std::string& value) {
			std::size_t start = 0;
			std::size_t end = value.size();
			while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		std::string NormalizeCanvasHostUrl(const std::string& baseCanvasHostUrl) {
			std::string normalized = TrimCopy(baseCanvasHostUrl);
			while (!normalized.empty() && normalized.back() == '/') {
				normalized.pop_back();
			}
			return normalized;
		}

		std::string BuildScopedCanvasHostUrl(
			const std::string& baseCanvasHostUrl,
			const std::string& capability) {
			const std::string normalized = NormalizeCanvasHostUrl(baseCanvasHostUrl);
			if (normalized.empty()) {
				return {};
			}
			return normalized + "/session/" + capability;
		}

	} // namespace

	std::uint64_t GatewayNodeCanvasCapabilityService::CapabilityTtlMs() noexcept {
		return 5ull * 60ull * 1000ull;
	}

	NodeCanvasCapabilityRefreshResult GatewayNodeCanvasCapabilityService::RefreshCapability(
		const std::string& sessionKey,
		const std::string& baseCanvasHostUrl) {
		NodeCanvasCapabilityRefreshResult result;
		const std::string normalizedSessionKey = TrimCopy(sessionKey);
		if (normalizedSessionKey.empty()) {
			result.error = "sessionKey required";
			return result;
		}

		const std::string scopedBaseUrl = NormalizeCanvasHostUrl(baseCanvasHostUrl);
		if (scopedBaseUrl.empty()) {
			result.error = "canvas host unavailable for this node session";
			return result;
		}

		const std::string capability =
			"canvas-capability-" + std::to_string(m_nextCapabilitySequence++);
		const std::uint64_t expiresAt = GatewayEpochMilliseconds() + CapabilityTtlMs();
		const std::string scopedUrl = BuildScopedCanvasHostUrl(scopedBaseUrl, capability);
		if (scopedUrl.empty()) {
			result.error = "failed to mint scoped canvas host URL";
			return result;
		}

		m_capabilityBySession.insert_or_assign(normalizedSessionKey, capability);
		result.ok = true;
		result.canvasCapability = capability;
		result.canvasCapabilityExpiresAtMs = expiresAt;
		result.canvasHostUrl = scopedUrl;
		return result;
	}

} // namespace blazeclaw::gateway
