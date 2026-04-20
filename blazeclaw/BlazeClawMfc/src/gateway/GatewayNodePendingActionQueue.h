#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::gateway {

	struct PendingNodeAction {
		std::string id;
		std::string nodeId;
		std::string command;
		std::string paramsJson;
		std::string idempotencyKey;
		std::uint64_t enqueuedAtMs = 0;
	};

	class GatewayNodePendingActionQueue {
	public:
		[[nodiscard]] PendingNodeAction Enqueue(
			const std::string& nodeId,
			const std::string& command,
			const std::string& paramsJson,
			const std::string& idempotencyKey,
			std::uint64_t nowMs);

		[[nodiscard]] std::vector<PendingNodeAction> PullAllowed(
			const std::string& nodeId,
			const std::vector<std::string>& allowedCommands,
			std::uint64_t nowMs);

		[[nodiscard]] std::vector<PendingNodeAction> Ack(
			const std::string& nodeId,
			const std::vector<std::string>& ackedIds,
			std::uint64_t nowMs);

		[[nodiscard]] std::vector<PendingNodeAction> List(
			const std::string& nodeId,
			std::uint64_t nowMs);

		[[nodiscard]] static std::uint64_t ActionTtlMs() noexcept;
		[[nodiscard]] static std::size_t MaxPerNode() noexcept;

	private:
		[[nodiscard]] std::vector<PendingNodeAction> Prune(
			const std::string& nodeId,
			std::uint64_t nowMs);

		std::unordered_map<std::string, std::vector<PendingNodeAction>> m_actionsByNodeId;
		std::uint64_t m_nextActionSequence = 1;
	};

} // namespace blazeclaw::gateway
