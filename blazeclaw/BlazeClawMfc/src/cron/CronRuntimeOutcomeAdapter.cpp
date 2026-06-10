#include "pch.h"

#include "CronRuntimeOutcomeAdapter.h"

namespace blazeclaw::cron {

	namespace {
		bool StartsWithHttpScheme(const std::string& value) {
			const std::string lowered = ToLowerCopy(TrimCopy(value));
			return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
		}
	}

	void ApplyRuntimeExecutionResult(
		CronTimerRunOutcome& outcome,
		const CronJson& runtimeResult) {
		bool projectedPrimaryTransportFields = false;
		bool projectedFailureTransportFields = false;
		bool hasDeliveredField = false;
		bool hasDeliveryStatusField = false;
		bool hasFailureDestinationStatusField = false;
		if (runtimeResult.contains("status") && runtimeResult["status"].is_string()) {
			outcome.status =
				ToLowerCopy(TrimCopy(runtimeResult["status"].get<std::string>()));
		}
		if (runtimeResult.contains("summary") && runtimeResult["summary"].is_string()) {
			outcome.summary = TrimCopy(runtimeResult["summary"].get<std::string>());
		}
		if (runtimeResult.contains("error") && runtimeResult["error"].is_string()) {
			outcome.error = TrimCopy(runtimeResult["error"].get<std::string>());
		}
		if (runtimeResult.contains("errorCategory") &&
			runtimeResult["errorCategory"].is_string()) {
			outcome.errorCategory =
				ToLowerCopy(TrimCopy(runtimeResult["errorCategory"].get<std::string>()));
		}
		if (runtimeResult.contains("retryable") && runtimeResult["retryable"].is_boolean()) {
			outcome.retryable = runtimeResult["retryable"].get<bool>();
		}
		if (runtimeResult.contains("retryAfterMs")) {
			const auto maybeRetryAfter = TryReadInt64Field(runtimeResult, "retryAfterMs");
			if (maybeRetryAfter.has_value() && maybeRetryAfter.value() >= 0) {
				outcome.hasRetryDelayOverride = true;
				outcome.retryDelayOverrideMs = maybeRetryAfter.value();
			}
		}
		if (runtimeResult.contains("skipDelivery") &&
			runtimeResult["skipDelivery"].is_boolean()) {
			outcome.skipDeliverySimulation = runtimeResult["skipDelivery"].get<bool>();
		}
		if (runtimeResult.contains("timedOut") && runtimeResult["timedOut"].is_boolean()) {
			outcome.timedOut = runtimeResult["timedOut"].get<bool>();
		}
		if (runtimeResult.contains("aborted") && runtimeResult["aborted"].is_boolean()) {
			outcome.aborted = runtimeResult["aborted"].get<bool>();
		}
		if (outcome.timedOut && outcome.errorCategory.empty()) {
			outcome.errorCategory = "timeout";
		}
		if (outcome.aborted && outcome.errorCategory.empty()) {
			outcome.errorCategory = "aborted";
		}
		if (runtimeResult.contains("sessionId") && runtimeResult["sessionId"].is_string()) {
			outcome.sessionId = TrimCopy(runtimeResult["sessionId"].get<std::string>());
		}
		if (runtimeResult.contains("sessionKey") && runtimeResult["sessionKey"].is_string()) {
			outcome.sessionKey = TrimCopy(runtimeResult["sessionKey"].get<std::string>());
		}
		if (runtimeResult.contains("model") && runtimeResult["model"].is_string()) {
			outcome.model = TrimCopy(runtimeResult["model"].get<std::string>());
		}
		if (runtimeResult.contains("provider") && runtimeResult["provider"].is_string()) {
			outcome.provider = TrimCopy(runtimeResult["provider"].get<std::string>());
		}
		if (runtimeResult.contains("runtimeModule") &&
			runtimeResult["runtimeModule"].is_string()) {
			outcome.runtimeModule =
				TrimCopy(runtimeResult["runtimeModule"].get<std::string>());
		}

		if (runtimeResult.contains("delivered") && runtimeResult["delivered"].is_boolean()) {
			outcome.delivered = runtimeResult["delivered"].get<bool>();
			hasDeliveredField = true;
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryStatus") && runtimeResult["deliveryStatus"].is_string()) {
			outcome.deliveryStatus = ToLowerCopy(
				TrimCopy(runtimeResult["deliveryStatus"].get<std::string>()));
			hasDeliveryStatusField = true;
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryMode") && runtimeResult["deliveryMode"].is_string()) {
			outcome.deliveryMode = ToLowerCopy(
				TrimCopy(runtimeResult["deliveryMode"].get<std::string>()));
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryTarget") && runtimeResult["deliveryTarget"].is_string()) {
			outcome.deliveryTarget = TrimCopy(runtimeResult["deliveryTarget"].get<std::string>());
			projectedPrimaryTransportFields = true;
		}
		if (outcome.deliveryMode.empty() &&
			!outcome.deliveryTarget.empty() &&
			StartsWithHttpScheme(outcome.deliveryTarget)) {
			outcome.deliveryMode = "webhook";
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryChannel") && runtimeResult["deliveryChannel"].is_string()) {
			outcome.deliveryChannel = TrimCopy(runtimeResult["deliveryChannel"].get<std::string>());
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryAccountId") && runtimeResult["deliveryAccountId"].is_string()) {
			outcome.deliveryAccountId = TrimCopy(runtimeResult["deliveryAccountId"].get<std::string>());
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryAttempted") && runtimeResult["deliveryAttempted"].is_boolean()) {
			outcome.deliveryAttempted = runtimeResult["deliveryAttempted"].get<bool>();
			projectedPrimaryTransportFields = true;
		}
		if (runtimeResult.contains("deliveryHttpStatus")) {
			const auto maybeStatus = TryReadInt64Field(runtimeResult, "deliveryHttpStatus");
			if (maybeStatus.has_value()) {
				outcome.deliveryHttpStatus = maybeStatus.value();
				projectedPrimaryTransportFields = true;
			}
		}

		if (runtimeResult.contains("failureDestinationStatus") &&
			runtimeResult["failureDestinationStatus"].is_string()) {
			outcome.failureDestinationStatus = ToLowerCopy(
				TrimCopy(runtimeResult["failureDestinationStatus"].get<std::string>()));
			hasFailureDestinationStatusField = true;
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationMode") &&
			runtimeResult["failureDestinationMode"].is_string()) {
			outcome.failureDestinationMode = ToLowerCopy(
				TrimCopy(runtimeResult["failureDestinationMode"].get<std::string>()));
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationTarget") &&
			runtimeResult["failureDestinationTarget"].is_string()) {
			outcome.failureDestinationTarget =
				TrimCopy(runtimeResult["failureDestinationTarget"].get<std::string>());
			projectedFailureTransportFields = true;
		}
		if (outcome.failureDestinationMode.empty() &&
			!outcome.failureDestinationTarget.empty() &&
			StartsWithHttpScheme(outcome.failureDestinationTarget)) {
			outcome.failureDestinationMode = "webhook";
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationChannel") &&
			runtimeResult["failureDestinationChannel"].is_string()) {
			outcome.failureDestinationChannel =
				TrimCopy(runtimeResult["failureDestinationChannel"].get<std::string>());
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationAccountId") &&
			runtimeResult["failureDestinationAccountId"].is_string()) {
			outcome.failureDestinationAccountId =
				TrimCopy(runtimeResult["failureDestinationAccountId"].get<std::string>());
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationAttempted") &&
			runtimeResult["failureDestinationAttempted"].is_boolean()) {
			outcome.failureDestinationAttempted =
				runtimeResult["failureDestinationAttempted"].get<bool>();
			projectedFailureTransportFields = true;
		}
		if (runtimeResult.contains("failureDestinationHttpStatus")) {
			const auto maybeFailureStatus =
				TryReadInt64Field(runtimeResult, "failureDestinationHttpStatus");
			if (maybeFailureStatus.has_value()) {
				outcome.failureDestinationHttpStatus = maybeFailureStatus.value();
				projectedFailureTransportFields = true;
			}
		}
		if (runtimeResult.contains("failureDestinationError") &&
			runtimeResult["failureDestinationError"].is_string()) {
			outcome.failureDestinationError =
				TrimCopy(runtimeResult["failureDestinationError"].get<std::string>());
			projectedFailureTransportFields = true;
		}

		outcome.runtimeProjectedPrimaryTransport = projectedPrimaryTransportFields;
		outcome.runtimeProjectedFailureDestinationTransport = projectedFailureTransportFields;
		outcome.runtimeProjectedTransport =
			projectedPrimaryTransportFields || projectedFailureTransportFields;

		if (projectedPrimaryTransportFields) {
			if (hasDeliveryStatusField) {
				if (!hasDeliveredField) {
					outcome.delivered = outcome.deliveryStatus == "delivered";
				}
			}
			else {
				if (hasDeliveredField) {
					if (outcome.delivered) {
						outcome.deliveryStatus = "delivered";
					}
					else if (outcome.deliveryAttempted) {
						outcome.deliveryStatus = "not-delivered";
					}
					else if (!outcome.deliveryTarget.empty() ||
						!outcome.deliveryMode.empty()) {
						outcome.deliveryStatus = "unknown";
					}
				}
				else if (outcome.deliveryAttempted ||
					!outcome.deliveryTarget.empty() ||
					!outcome.deliveryMode.empty()) {
					outcome.deliveryStatus = "unknown";
				}
			}
		}

		if (projectedFailureTransportFields &&
			!hasFailureDestinationStatusField) {
			if (outcome.failureDestinationAttempted ||
				!outcome.failureDestinationTarget.empty() ||
				!outcome.failureDestinationMode.empty()) {
				outcome.failureDestinationStatus = "unknown";
			}
		}

		if (runtimeResult.contains("usage") && runtimeResult["usage"].is_object()) {
			const CronJson& usage = runtimeResult["usage"];
			const auto promptTokens = TryReadInt64Field(usage, "promptTokens");
			const auto completionTokens = TryReadInt64Field(usage, "completionTokens");
			const auto totalTokens = TryReadInt64Field(usage, "totalTokens");
			if (promptTokens.has_value() ||
				completionTokens.has_value() ||
				totalTokens.has_value()) {
				outcome.usagePromptTokens = promptTokens.value_or(0);
				outcome.usageCompletionTokens = completionTokens.value_or(0);
				outcome.usageTotalTokens = totalTokens.has_value()
					? totalTokens.value()
					: (outcome.usagePromptTokens + outcome.usageCompletionTokens);
				outcome.usageAvailable = true;
			}
		}
	}

} // namespace blazeclaw::cron
