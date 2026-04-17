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

		const auto structuralSignals =
			prompt::AnalyzeOrchestrationStructuralSignals(input.message);
		output.weatherEmailIntent =
			prompt::AnalyzeWeatherEmailPromptIntent(input.message);
		output.weatherEmailIntent.hasWeather =
			structuralSignals.hasWeatherCapabilityIntent;
		output.weatherEmailIntent.hasEmail =
			structuralSignals.hasEmailCapabilityIntent;
		output.weatherEmailIntent.hasReport =
			structuralSignals.hasReportIntent;
		output.weatherEmailIntent.hasRecipient =
			structuralSignals.hasRecipient;
		output.weatherEmailIntent.hasSchedule =
			structuralSignals.hasScheduleIntent;
		output.weatherEmailIntent.city = structuralSignals.city;
		output.weatherEmailIntent.date = structuralSignals.date;
		output.weatherEmailIntent.recipient = structuralSignals.recipient;
		output.weatherEmailIntent.sendAt = structuralSignals.sendAt;
		output.weatherEmailIntent.scheduleKind = structuralSignals.scheduleKind;
		output.weatherEmailIntent.missReasons = structuralSignals.missReasons;
		output.weatherEmailIntent.matched =
			structuralSignals.weatherEmailFlowCandidate;
		output.compatDeterministicEnabled =
			output.selectedPath == "runtime_orchestration";
		output.intentDeterministicEnabled =
			!input.forceError &&
			!input.hasAttachments &&
			structuralSignals.weatherEmailFlowCandidate;
		output.deterministicEnabled =
			output.compatDeterministicEnabled ||
			output.intentDeterministicEnabled;
		if (output.intentDeterministicEnabled) {
			auto addOrderedTarget = [&output](const std::string& target) {
				if (target.empty()) {
					return;
				}

				if (std::find(
					output.orderedPolicyTargets.begin(),
					output.orderedPolicyTargets.end(),
					target) != output.orderedPolicyTargets.end()) {
					return;
				}

				output.orderedPolicyTargets.push_back(target);
				};

			output.orderedPolicyMode = "strict";
			output.orderedPolicyStrict = true;

			if (output.weatherEmailIntent.hasWeather) {
				addOrderedTarget("weather.lookup");
			}

			if (output.weatherEmailIntent.hasEmail) {
				addOrderedTarget("email.schedule");
			}

			if (output.orderedPolicyTargets.size() < 2) {
				output.orderedPolicyMode = "advisory";
				output.orderedPolicyStrict = false;
			}

			output.fallbackPolicyProfile =
				"strict_ordered_required_capabilities";
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
			structuralSignals.weatherEmailFlowCandidate
			? "structural_orchestration_signals"
			: "none";

		if (output.deterministicEnabled) {
			output.orderedPolicyDecision =
				"enforce_policy_ordered_sequence";
			output.allowlistPolicyHint =
				"allowlist_driven_by_ordered_sequence_policy";
			output.fallbackPolicyHint =
				"runtime_recovery_policy_after_ordered_sequence";
			if (output.fallbackPolicyProfile == "runtime_recovery_default") {
				output.fallbackPolicyProfile = "ordered_runtime_resilience";
			}
		}

		return output;
	}

} // namespace blazeclaw::gateway
