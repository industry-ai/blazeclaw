#pragma once

#include "GatewayJsonUtils.h"

#include <string>

namespace blazeclaw::gateway {

	class ChatOrchestrationPolicy {
	public:
		struct Input {
			std::string orchestrationPath;
			std::string message;
			bool forceError = false;
			bool hasAttachments = false;
		};

		struct Output {
			std::string selectedPath;
			bool compatDeterministicEnabled = false;
			bool intentDeterministicEnabled = false;
			bool deterministicEnabled = false;
			std::string decisionReasonCode = "policy.dynamic.default";
			std::string decompositionMetadataSource = "none";
			std::string orderedPolicyDecision =
				"defer_to_runtime_sequencing_preflight";
			std::string allowlistPolicyHint =
				"defer_to_tool_policy_pipeline";
			std::string fallbackPolicyHint =
				"defer_to_runtime_recovery_policy";
			prompt::WeatherEmailPromptIntent weatherEmailIntent;
		};

		[[nodiscard]] static Output Evaluate(const Input& input);
	};

} // namespace blazeclaw::gateway
