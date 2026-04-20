#include "pch.h"
#include "GatewayNodePairingService.h"

#include "GatewayHostProtocolHelpers.h"

#include <algorithm>
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

		std::string ToLowerAscii(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			return value;
		}

		bool ContainsScope(
			const std::vector<std::string>& callerScopes,
			const std::string& targetScope) {
			if (targetScope.empty()) {
				return true;
			}
			const std::string normalizedTarget = ToLowerAscii(targetScope);
			for (const std::string& raw : callerScopes) {
				if (ToLowerAscii(TrimCopy(raw)) == normalizedTarget) {
					return true;
				}
			}
			return false;
		}

		std::vector<std::string> ResolveRequiredScopes(const std::vector<std::string>& commands) {
			for (const std::string& command : commands) {
				if (command.rfind("system.", 0) == 0) {
					return { "node.pair.approve.system" };
				}
				if (command.rfind("camera.", 0) == 0 || command.rfind("screen.", 0) == 0) {
					return { "node.pair.approve.media" };
				}
			}
			return { "node.pair.approve" };
		}

	} // namespace

	NodePairingRequestResult GatewayNodePairingService::RequestPairing(
		const NodePairingDeclaredSurface& declared,
		const bool silent) {
		const std::string normalizedNodeId = TrimCopy(declared.nodeId);
		if (normalizedNodeId.empty()) {
			return {};
		}

		NodePairingPendingRequest* existing = nullptr;
		for (auto& [_, pending] : m_pendingByRequestId) {
			if (pending.declared.nodeId == normalizedNodeId) {
				existing = &pending;
				break;
			}
		}

		if (existing != nullptr) {
			existing->declared = declared;
			existing->declared.nodeId = normalizedNodeId;
			existing->silent = existing->silent && silent;
			existing->tsMs = GatewayEpochMilliseconds();
			existing->requiredApproveScopes = ResolveRequiredScopes(existing->declared.commands);
			return NodePairingRequestResult{ .request = *existing, .created = false };
		}

		NodePairingPendingRequest pending;
		pending.requestId = "node-pair-request-" + std::to_string(m_nextRequestSequence++);
		pending.declared = declared;
		pending.declared.nodeId = normalizedNodeId;
		pending.silent = silent;
		pending.tsMs = GatewayEpochMilliseconds();
		pending.requiredApproveScopes = ResolveRequiredScopes(pending.declared.commands);
		m_pendingByRequestId.insert_or_assign(pending.requestId, pending);

		return NodePairingRequestResult{ .request = pending, .created = true };
	}

	NodePairingListResult GatewayNodePairingService::ListPairing() const {
		NodePairingListResult result;
		result.pending.reserve(m_pendingByRequestId.size());
		for (const auto& [_, pending] : m_pendingByRequestId) {
			result.pending.push_back(pending);
		}
		std::sort(
			result.pending.begin(),
			result.pending.end(),
			[](const NodePairingPendingRequest& left, const NodePairingPendingRequest& right) {
				return left.tsMs > right.tsMs;
			});

		result.paired.reserve(m_pairedByNodeId.size());
		for (const auto& [_, paired] : m_pairedByNodeId) {
			result.paired.push_back(paired);
		}
		std::sort(
			result.paired.begin(),
			result.paired.end(),
			[](const NodePairingPairedNode& left, const NodePairingPairedNode& right) {
				return left.approvedAtMs > right.approvedAtMs;
			});

		return result;
	}

	NodePairingApproveResult GatewayNodePairingService::ApprovePairing(
		const std::string& requestId,
		const std::vector<std::string>& callerScopes) {
		const auto it = m_pendingByRequestId.find(TrimCopy(requestId));
		if (it == m_pendingByRequestId.end()) {
			return NodePairingApproveResult{ .kind = NodePairingApproveResult::Kind::NotFound };
		}

		const NodePairingPendingRequest pending = it->second;
		for (const std::string& requiredScope : pending.requiredApproveScopes) {
			if (!ContainsScope(callerScopes, requiredScope)) {
				return NodePairingApproveResult{
					.kind = NodePairingApproveResult::Kind::Forbidden,
					.requestId = pending.requestId,
					.missingScope = requiredScope,
				};
			}
		}

		const std::uint64_t nowMs = GatewayEpochMilliseconds();
		const auto existing = m_pairedByNodeId.find(pending.declared.nodeId);
		const std::uint64_t createdAtMs =
			existing != m_pairedByNodeId.end() ? existing->second.createdAtMs : nowMs;

		NodePairingPairedNode node;
		node.declared = pending.declared;
		node.token = "node-token-" + std::to_string(m_nextTokenSequence++);
		node.createdAtMs = createdAtMs;
		node.approvedAtMs = nowMs;

		m_pairedByNodeId.insert_or_assign(node.declared.nodeId, node);
		m_pendingByRequestId.erase(it);

		return NodePairingApproveResult{
			.kind = NodePairingApproveResult::Kind::Approved,
			.requestId = pending.requestId,
			.node = node,
		};
	}

	NodePairingRejectResult GatewayNodePairingService::RejectPairing(const std::string& requestId) {
		const auto it = m_pendingByRequestId.find(TrimCopy(requestId));
		if (it == m_pendingByRequestId.end()) {
			return {};
		}

		NodePairingRejectResult result;
		result.found = true;
		result.requestId = it->second.requestId;
		result.nodeId = it->second.declared.nodeId;
		m_pendingByRequestId.erase(it);
		return result;
	}

	bool GatewayNodePairingService::VerifyNodeToken(
		const std::string& nodeId,
		const std::string& token,
		NodePairingPairedNode* outNode) const {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return false;
		}

		const auto it = m_pairedByNodeId.find(normalizedNodeId);
		if (it == m_pairedByNodeId.end()) {
			return false;
		}

		if (it->second.token != token) {
			return false;
		}

		if (outNode != nullptr) {
			*outNode = it->second;
		}
		return true;
	}

	std::optional<NodePairingPairedNode> GatewayNodePairingService::RenamePairedNode(
		const std::string& nodeId,
		const std::string& displayName) {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		const std::string trimmedDisplayName = TrimCopy(displayName);
		if (normalizedNodeId.empty() || trimmedDisplayName.empty()) {
			return std::nullopt;
		}

		const auto it = m_pairedByNodeId.find(normalizedNodeId);
		if (it == m_pairedByNodeId.end()) {
			return std::nullopt;
		}

		it->second.declared.displayName = trimmedDisplayName;
		return it->second;
	}

	std::vector<NodePairingPairedNode> GatewayNodePairingService::ListPairedNodes() const {
		std::vector<NodePairingPairedNode> paired;
		paired.reserve(m_pairedByNodeId.size());
		for (const auto& [_, node] : m_pairedByNodeId) {
			paired.push_back(node);
		}
		std::sort(
			paired.begin(),
			paired.end(),
			[](const NodePairingPairedNode& left, const NodePairingPairedNode& right) {
				return left.approvedAtMs > right.approvedAtMs;
			});
		return paired;
	}

} // namespace blazeclaw::gateway
