#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace blazeclaw::gateway {

	struct NodeWakeAttempt {
		bool available = false;
		bool throttled = false;
		std::string path;
		std::uint64_t durationMs = 0;
		int apnsStatus = -1;
		std::string apnsReason;
	};

	struct NodeWakeNudgeAttempt {
		bool sent = false;
		bool throttled = false;
		std::string reason;
		std::uint64_t durationMs = 0;
		int apnsStatus = -1;
		std::string apnsReason;
	};

	class GatewayNodeWakeService {
	public:
		[[nodiscard]] NodeWakeAttempt MaybeWakeNode(
			const std::string& nodeId,
			bool force,
			const std::string& wakeReason,
			std::uint64_t nowMs);

		[[nodiscard]] bool WaitForNodeReconnect(
			const std::string& nodeId,
			std::uint64_t timeoutMs,
			std::uint64_t pollMs,
			std::uint64_t nowMs) const;

		[[nodiscard]] NodeWakeNudgeAttempt MaybeSendWakeNudge(
			const std::string& nodeId,
			std::uint64_t nowMs);

		void ClearNodeWakeState(const std::string& nodeId);

		[[nodiscard]] static std::uint64_t ReconnectWaitMs() noexcept;
		[[nodiscard]] static std::uint64_t ReconnectRetryWaitMs() noexcept;
		[[nodiscard]] static std::uint64_t ReconnectPollMs() noexcept;

	private:
		struct NodeWakeState {
			std::uint64_t lastWakeAtMs = 0;
		};

		std::unordered_map<std::string, NodeWakeState> m_wakeByNodeId;
		std::unordered_map<std::string, std::uint64_t> m_nudgeByNodeId;
	};

} // namespace blazeclaw::gateway
