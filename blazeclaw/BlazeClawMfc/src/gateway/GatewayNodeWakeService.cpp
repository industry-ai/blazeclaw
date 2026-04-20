#include "pch.h"
#include "GatewayNodeWakeService.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway {
	namespace {

		constexpr std::uint64_t kWakeThrottleMs = 15ull * 1000ull;
		constexpr std::uint64_t kWakeNudgeThrottleMs = 10ull * 60ull * 1000ull;

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

	} // namespace

	std::uint64_t GatewayNodeWakeService::ReconnectWaitMs() noexcept {
		return 3ull * 1000ull;
	}

	std::uint64_t GatewayNodeWakeService::ReconnectRetryWaitMs() noexcept {
		return 12ull * 1000ull;
	}

	std::uint64_t GatewayNodeWakeService::ReconnectPollMs() noexcept {
		return 150ull;
	}

	NodeWakeAttempt GatewayNodeWakeService::MaybeWakeNode(
		const std::string& nodeId,
		const bool force,
		const std::string& wakeReason,
		const std::uint64_t nowMs) {
		NodeWakeAttempt attempt;
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			attempt.path = "no-registration";
			attempt.apnsReason = "nodeId required";
			return attempt;
		}

		NodeWakeState& state = m_wakeByNodeId[normalizedNodeId];
		if (!force && state.lastWakeAtMs > 0 && nowMs > state.lastWakeAtMs && (nowMs - state.lastWakeAtMs) < kWakeThrottleMs) {
			attempt.available = true;
			attempt.throttled = true;
			attempt.path = "throttled";
			attempt.apnsReason = "wake throttled";
			return attempt;
		}

		state.lastWakeAtMs = nowMs;
		attempt.available = true;
		attempt.throttled = false;
		attempt.path = "sent";
		attempt.apnsStatus = 200;
		attempt.apnsReason = wakeReason.empty() ? "node.invoke" : wakeReason;
		attempt.durationMs = 1;
		return attempt;
	}

	bool GatewayNodeWakeService::WaitForNodeReconnect(
		const std::string& nodeId,
		const std::uint64_t timeoutMs,
		const std::uint64_t pollMs,
		const std::uint64_t nowMs) const {
		(void)pollMs;
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return false;
		}

		const auto it = m_wakeByNodeId.find(normalizedNodeId);
		if (it == m_wakeByNodeId.end()) {
			return false;
		}

		const std::uint64_t elapsed = nowMs >= it->second.lastWakeAtMs
			? nowMs - it->second.lastWakeAtMs
			: 0;
		return elapsed <= std::max<std::uint64_t>(250, timeoutMs);
	}

	NodeWakeNudgeAttempt GatewayNodeWakeService::MaybeSendWakeNudge(
		const std::string& nodeId,
		const std::uint64_t nowMs) {
		NodeWakeNudgeAttempt attempt;
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			attempt.reason = "no-registration";
			attempt.apnsReason = "nodeId required";
			return attempt;
		}

		const auto it = m_nudgeByNodeId.find(normalizedNodeId);
		if (it != m_nudgeByNodeId.end() && nowMs > it->second && (nowMs - it->second) < kWakeNudgeThrottleMs) {
			attempt.sent = false;
			attempt.throttled = true;
			attempt.reason = "throttled";
			attempt.apnsReason = "nudge throttled";
			return attempt;
		}

		m_nudgeByNodeId.insert_or_assign(normalizedNodeId, nowMs);
		attempt.sent = true;
		attempt.throttled = false;
		attempt.reason = "sent";
		attempt.apnsStatus = 200;
		attempt.apnsReason = "nudge sent";
		attempt.durationMs = 1;
		return attempt;
	}

	void GatewayNodeWakeService::ClearNodeWakeState(const std::string& nodeId) {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return;
		}
		m_wakeByNodeId.erase(normalizedNodeId);
		m_nudgeByNodeId.erase(normalizedNodeId);
	}

} // namespace blazeclaw::gateway
