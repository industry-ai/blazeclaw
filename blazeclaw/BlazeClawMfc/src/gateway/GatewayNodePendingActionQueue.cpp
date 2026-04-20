#include "pch.h"
#include "GatewayNodePendingActionQueue.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

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

		std::string ToLowerAscii(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return value;
		}

		std::unordered_set<std::string> NormalizeAllowSet(const std::vector<std::string>& commands) {
			std::unordered_set<std::string> normalized;
			for (const auto& command : commands) {
				const std::string trimmed = TrimCopy(command);
				if (trimmed.empty()) {
					continue;
				}
				normalized.insert(ToLowerAscii(trimmed));
			}
			return normalized;
		}

		bool IsCommandAllowed(
			const std::string& command,
			const std::unordered_set<std::string>& allowedCommands) {
			if (allowedCommands.empty()) {
				return true;
			}
			return allowedCommands.find(ToLowerAscii(command)) != allowedCommands.end();
		}

	} // namespace

	std::uint64_t GatewayNodePendingActionQueue::ActionTtlMs() noexcept {
		return 10ull * 60ull * 1000ull;
	}

	std::size_t GatewayNodePendingActionQueue::MaxPerNode() noexcept {
		return 64;
	}

	std::vector<PendingNodeAction> GatewayNodePendingActionQueue::Prune(
		const std::string& nodeId,
		const std::uint64_t nowMs) {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return {};
		}

		const auto it = m_actionsByNodeId.find(normalizedNodeId);
		if (it == m_actionsByNodeId.end()) {
			return {};
		}

		const std::uint64_t minTimestamp = nowMs > ActionTtlMs() ? nowMs - ActionTtlMs() : 0;
		std::vector<PendingNodeAction> live;
		live.reserve(it->second.size());
		for (const auto& action : it->second) {
			if (action.enqueuedAtMs >= minTimestamp) {
				live.push_back(action);
			}
		}

		if (live.empty()) {
			m_actionsByNodeId.erase(normalizedNodeId);
			return {};
		}

		m_actionsByNodeId.insert_or_assign(normalizedNodeId, live);
		return live;
	}

	PendingNodeAction GatewayNodePendingActionQueue::Enqueue(
		const std::string& nodeId,
		const std::string& command,
		const std::string& paramsJson,
		const std::string& idempotencyKey,
		const std::uint64_t nowMs) {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		const std::string normalizedCommand = TrimCopy(command);
		const std::string normalizedIdempotencyKey = TrimCopy(idempotencyKey);
		if (normalizedNodeId.empty() || normalizedCommand.empty() || normalizedIdempotencyKey.empty()) {
			return {};
		}

		std::vector<PendingNodeAction> queue = Prune(normalizedNodeId, nowMs);
		for (const auto& action : queue) {
			if (action.idempotencyKey == normalizedIdempotencyKey) {
				return action;
			}
		}

		PendingNodeAction action;
		action.id = "node-pending-action-" + std::to_string(m_nextActionSequence++);
		action.nodeId = normalizedNodeId;
		action.command = normalizedCommand;
		action.paramsJson = paramsJson;
		action.idempotencyKey = normalizedIdempotencyKey;
		action.enqueuedAtMs = nowMs;
		queue.push_back(action);

		if (queue.size() > MaxPerNode()) {
			queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(queue.size() - MaxPerNode()));
		}

		m_actionsByNodeId.insert_or_assign(normalizedNodeId, queue);
		return action;
	}

	std::vector<PendingNodeAction> GatewayNodePendingActionQueue::PullAllowed(
		const std::string& nodeId,
		const std::vector<std::string>& allowedCommands,
		const std::uint64_t nowMs) {
		std::vector<PendingNodeAction> queue = Prune(nodeId, nowMs);
		if (queue.empty()) {
			return {};
		}

		const std::unordered_set<std::string> allowSet = NormalizeAllowSet(allowedCommands);
		std::vector<PendingNodeAction> allowed;
		allowed.reserve(queue.size());
		for (const auto& action : queue) {
			if (IsCommandAllowed(action.command, allowSet)) {
				allowed.push_back(action);
			}
		}

		if (allowed.size() != queue.size()) {
			const std::string normalizedNodeId = TrimCopy(nodeId);
			if (allowed.empty()) {
				m_actionsByNodeId.erase(normalizedNodeId);
			}
			else {
				m_actionsByNodeId.insert_or_assign(normalizedNodeId, allowed);
			}
		}

		return allowed;
	}

	std::vector<PendingNodeAction> GatewayNodePendingActionQueue::Ack(
		const std::string& nodeId,
		const std::vector<std::string>& ackedIds,
		const std::uint64_t nowMs) {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return {};
		}

		std::vector<PendingNodeAction> queue = Prune(normalizedNodeId, nowMs);
		if (queue.empty()) {
			return {};
		}

		if (ackedIds.empty()) {
			return queue;
		}

		std::unordered_set<std::string> ackSet;
		for (const auto& id : ackedIds) {
			const std::string trimmed = TrimCopy(id);
			if (!trimmed.empty()) {
				ackSet.insert(trimmed);
			}
		}

		std::vector<PendingNodeAction> remaining;
		remaining.reserve(queue.size());
		for (const auto& action : queue) {
			if (ackSet.find(action.id) == ackSet.end()) {
				remaining.push_back(action);
			}
		}

		if (remaining.empty()) {
			m_actionsByNodeId.erase(normalizedNodeId);
			return {};
		}

		m_actionsByNodeId.insert_or_assign(normalizedNodeId, remaining);
		return remaining;
	}

	std::vector<PendingNodeAction> GatewayNodePendingActionQueue::List(
		const std::string& nodeId,
		const std::uint64_t nowMs) {
		return Prune(nodeId, nowMs);
	}

} // namespace blazeclaw::gateway
