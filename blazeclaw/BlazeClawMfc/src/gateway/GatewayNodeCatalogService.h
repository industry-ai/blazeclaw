#pragma once

#include "GatewayNodePairingService.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

	struct KnownNodeSnapshot {
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
		std::vector<std::string> pairedVia;
		bool connected = false;
	};

	class GatewayNodeCatalogService {
	public:
		[[nodiscard]] std::vector<KnownNodeSnapshot> BuildKnownNodes(
			const std::vector<NodePairingPairedNode>& pairedNodes) const;

		[[nodiscard]] std::optional<KnownNodeSnapshot> FindKnownNode(
			const std::vector<KnownNodeSnapshot>& nodes,
			const std::string& nodeId) const;
	};

} // namespace blazeclaw::gateway
