#pragma once

#include "GatewayHost.h"

namespace blazeclaw::gateway {

	struct OrderedSequencePreflight {
		bool enforced = false;
		bool strictAllowlist = false;
		std::vector<std::string> orderedTargets;
		std::vector<std::string> explicitCallTargets;
		std::vector<std::string> resolvedToolTargets;
		std::vector<std::string> missingTargets;
		std::vector<std::string> missingResolvedToolTargets;
	};

	struct OrderedSequencePolicyOverride {
		std::vector<std::string> orderedTargets;
		bool strictAllowlist = false;
		std::string source;
	};

	class RuntimeSequencingPolicy {
	public:
		[[nodiscard]] static OrderedSequencePreflight BuildOrderedSequencePreflight(
			const std::string& message,
			const std::vector<ToolCatalogEntry>& tools,
			const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries,
			const OrderedSequencePolicyOverride* policyOverride = nullptr);

		[[nodiscard]] static bool IsResolvedRuntimeToolTarget(
			const std::string& resolvedToolId,
			const std::vector<ToolCatalogEntry>& runtimeTools);

		[[nodiscard]] static std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			BuildOrderedPreflightTaskDeltas(
				const std::string& runId,
				const std::string& sessionKey,
				const OrderedSequencePreflight& preflight,
				bool terminalFailure,
				const std::string& terminalErrorCode,
				const std::string& terminalErrorMessage);

		[[nodiscard]] static std::string JoinOrderedTargets(
			const std::vector<std::string>& targets);

		[[nodiscard]] static std::string JoinOrderedResolution(
			const OrderedSequencePreflight& preflight);
	};

} // namespace blazeclaw::gateway
