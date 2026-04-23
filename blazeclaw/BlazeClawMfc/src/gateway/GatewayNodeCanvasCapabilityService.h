#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace blazeclaw::gateway {

	struct NodeCanvasCapabilityRefreshResult {
		bool ok = false;
		std::string canvasCapability;
		std::uint64_t canvasCapabilityExpiresAtMs = 0;
		std::string canvasHostUrl;
		std::string error;
	};

	class GatewayNodeCanvasCapabilityService {
	public:
		[[nodiscard]] NodeCanvasCapabilityRefreshResult RefreshCapability(
			const std::string& sessionKey,
			const std::string& baseCanvasHostUrl);

		[[nodiscard]] bool VerifyCapabilityAndRefreshTtl(const std::string& capability);

		[[nodiscard]] static std::uint64_t CapabilityTtlMs() noexcept;

	private:
		struct CapabilityEntry {
			std::string capability;
			std::uint64_t expiresAtMs = 0;
		};

		std::unordered_map<std::string, CapabilityEntry> m_capabilityBySession;
		std::uint64_t m_nextCapabilitySequence = 1;
	};

} // namespace blazeclaw::gateway
