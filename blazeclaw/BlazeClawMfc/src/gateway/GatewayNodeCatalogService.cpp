#include "pch.h"
#include "GatewayNodeCatalogService.h"

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

	} // namespace

	std::vector<KnownNodeSnapshot> GatewayNodeCatalogService::BuildKnownNodes(
		const std::vector<NodePairingPairedNode>& pairedNodes) const {
		std::vector<KnownNodeSnapshot> nodes;
		nodes.reserve(pairedNodes.size());

		for (const auto& paired : pairedNodes) {
			KnownNodeSnapshot snapshot;
			snapshot.nodeId = paired.declared.nodeId;
			snapshot.displayName = paired.declared.displayName;
			snapshot.platform = paired.declared.platform;
			snapshot.version = paired.declared.version;
			snapshot.coreVersion = paired.declared.coreVersion;
			snapshot.uiVersion = paired.declared.uiVersion;
			snapshot.deviceFamily = paired.declared.deviceFamily;
			snapshot.modelIdentifier = paired.declared.modelIdentifier;
			snapshot.caps = paired.declared.caps;
			snapshot.commands = paired.declared.commands;
			snapshot.pairedVia = { "node.pair" };
			snapshot.connected = false;
			nodes.push_back(std::move(snapshot));
		}

		std::sort(
			nodes.begin(),
			nodes.end(),
			[](const KnownNodeSnapshot& left, const KnownNodeSnapshot& right) {
				return left.nodeId < right.nodeId;
			});

		return nodes;
	}

	std::optional<KnownNodeSnapshot> GatewayNodeCatalogService::FindKnownNode(
		const std::vector<KnownNodeSnapshot>& nodes,
		const std::string& nodeId) const {
		const std::string normalizedNodeId = TrimCopy(nodeId);
		if (normalizedNodeId.empty()) {
			return std::nullopt;
		}

		for (const auto& node : nodes) {
			if (node.nodeId == normalizedNodeId) {
				return node;
			}
		}

		return std::nullopt;
	}

} // namespace blazeclaw::gateway
