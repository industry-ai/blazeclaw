#pragma once

#include <optional>
#include <string>

namespace blazeclaw::gateway {

	class GatewayLifecycleEventEmitter {
	public:
		[[nodiscard]] static std::string BuildLifecyclePayload(
			const std::string& runId,
			const std::string& sessionKey,
			const std::string& state,
			std::uint64_t timestampMs,
			const std::optional<std::string>& reason = std::nullopt);

		static void EmitLifecycle(
			const std::string& state,
			const std::string& runId,
			const std::string& sessionKey,
			std::uint64_t timestampMs,
			const std::optional<std::string>& reason = std::nullopt);
	};

} // namespace blazeclaw::gateway
