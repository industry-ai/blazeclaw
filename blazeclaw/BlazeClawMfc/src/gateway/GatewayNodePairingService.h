#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::gateway {

	struct NodePairingDeclaredSurface {
		std::string nodeId;
		std::string displayName;
		std::string platform;
		std::string version;
		std::string coreVersion;
		std::string uiVersion;
		std::string deviceFamily;
		std::string modelIdentifier;
		std::vector<std::string> caps;
		std::vector<std::string> commands;
		std::unordered_map<std::string, bool> permissions;
		std::string remoteIp;
	};

	struct NodePairingPendingRequest {
		std::string requestId;
		NodePairingDeclaredSurface declared;
		bool silent = false;
		std::uint64_t tsMs = 0;
		std::vector<std::string> requiredApproveScopes;
	};

	struct NodePairingPairedNode {
		NodePairingDeclaredSurface declared;
		std::string token;
		std::uint64_t createdAtMs = 0;
		std::uint64_t approvedAtMs = 0;
		std::optional<std::uint64_t> lastConnectedAtMs;
	};

	struct NodePairingListResult {
		std::vector<NodePairingPendingRequest> pending;
		std::vector<NodePairingPairedNode> paired;
	};

	struct NodePairingRequestResult {
		NodePairingPendingRequest request;
		bool created = false;
	};

	struct NodePairingApproveResult {
		enum class Kind {
			Approved,
			Forbidden,
			NotFound,
		};

		Kind kind = Kind::NotFound;
		std::string requestId;
		NodePairingPairedNode node;
		std::string missingScope;
	};

	struct NodePairingRejectResult {
		bool found = false;
		std::string requestId;
		std::string nodeId;
	};

	class GatewayNodePairingService {
	public:
		[[nodiscard]] NodePairingRequestResult RequestPairing(
			const NodePairingDeclaredSurface& declared,
			bool silent);

		[[nodiscard]] NodePairingListResult ListPairing() const;

		[[nodiscard]] NodePairingApproveResult ApprovePairing(
			const std::string& requestId,
			const std::vector<std::string>& callerScopes);

		[[nodiscard]] NodePairingRejectResult RejectPairing(const std::string& requestId);

		[[nodiscard]] bool VerifyNodeToken(
			const std::string& nodeId,
			const std::string& token,
			NodePairingPairedNode* outNode = nullptr) const;

		[[nodiscard]] std::optional<NodePairingPairedNode> RenamePairedNode(
			const std::string& nodeId,
			const std::string& displayName);

		[[nodiscard]] std::vector<NodePairingPairedNode> ListPairedNodes() const;
		[[nodiscard]] bool RemovePairedNode(const std::string& nodeId, NodePairingPairedNode* removedNode = nullptr);

	private:
		std::unordered_map<std::string, NodePairingPendingRequest> m_pendingByRequestId;
		std::unordered_map<std::string, NodePairingPairedNode> m_pairedByNodeId;
		std::uint64_t m_nextRequestSequence = 1;
		std::uint64_t m_nextTokenSequence = 1;
	};

} // namespace blazeclaw::gateway
