#include "pch.h"
#include "ChatOrchestrationPolicy.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::gateway {
	namespace {
		std::string ToLowerCopyPolicy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}
	}

	ChatOrchestrationPolicy::Output ChatOrchestrationPolicy::Evaluate(
		const Input& input) {
		Output output;
		output.selectedPath = ToLowerCopyPolicy(input.orchestrationPath);
		if (output.selectedPath.empty()) {
			output.selectedPath = "dynamic_task_delta";
		}

		output.weatherEmailIntent =
			prompt::AnalyzeWeatherEmailPromptIntent(input.message);
		output.compatDeterministicEnabled =
			output.selectedPath == "runtime_orchestration";
		output.intentDeterministicEnabled =
			!input.forceError &&
			!input.hasAttachments &&
			output.weatherEmailIntent.matched;
		output.deterministicEnabled =
			output.compatDeterministicEnabled ||
			output.intentDeterministicEnabled;
		if (output.intentDeterministicEnabled) {
			output.orderedPolicyMode = "strict";
			output.orderedPolicyStrict = true;
			output.orderedPolicyTargets = {
				"weather.lookup",
				"report.compose",
				"email.schedule",
			};
		}

		if (output.compatDeterministicEnabled &&
			output.intentDeterministicEnabled) {
			output.decisionReasonCode =
				"policy.deterministic.compat_and_intent";
		}
		else if (output.compatDeterministicEnabled) {
			output.decisionReasonCode =
				"policy.deterministic.compat_path";
		}
		else if (output.intentDeterministicEnabled) {
			output.decisionReasonCode =
				"policy.deterministic.intent_override";
		}
		else {
			output.decisionReasonCode = "policy.dynamic.default";
		}

		output.decompositionMetadataSource =
			output.weatherEmailIntent.matched
			? "weather_email_structural_intent"
			: "none";

		if (output.deterministicEnabled) {
			output.orderedPolicyDecision =
				"enforce_policy_ordered_sequence";
			output.allowlistPolicyHint =
				"allowlist_driven_by_ordered_sequence_policy";
			output.fallbackPolicyHint =
				"runtime_recovery_policy_after_ordered_sequence";
		}

		return output;
	}

} // namespace blazeclaw::gateway
