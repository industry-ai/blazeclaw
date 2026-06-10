#pragma once

#include "CronModels.h"

#include <string>

namespace blazeclaw::cron {

	// Shared timer run-outcome projection surface for runtime adapters and tests.
	// Phase DI Step 3: extracted from CronTimerService for adapter-facing parity work.
	struct CronTimerRunOutcome {
		std::string status = "ok";
		std::string summary;
		std::string error;
		std::string runtimeExecutionPath = "none";
		std::string runtimeModule;
		bool runtimeAdapterRegistered = false;
		bool runtimeAdapterInvoked = false;
		bool runtimeHandled = false;
		bool simulationFallbackUsed = false;
		std::string sessionId;
		std::string sessionKey;
		std::string model;
		std::string provider;
		std::int64_t usagePromptTokens = 0;
		std::int64_t usageCompletionTokens = 0;
		std::int64_t usageTotalTokens = 0;
		bool usageAvailable = false;
		std::string deliveryStatus = "not-requested";
		std::string deliveryMode;
		std::string deliveryTarget;
		std::string deliveryChannel;
		std::string deliveryAccountId;
		bool delivered = false;
		bool deliveryAttempted = false;
		bool deliveryChannelIoPerformed = false;
		std::string deliverySemantics;
		std::int64_t deliveryHttpStatus = 0;
		bool retryable = false;
		std::string failureDestinationStatus = "not-requested";
		std::string failureDestinationTarget;
		std::string failureDestinationChannel;
		std::string failureDestinationAccountId;
		bool failureDestinationAttempted = false;
		std::int64_t failureDestinationHttpStatus = 0;
		std::string failureDestinationError;
		std::string failureDestinationMode;
		std::string failureAlertStatus;
		bool failureAlertAttempted = false;
		std::int64_t failureAlertHttpStatus = 0;
		std::string failureAlertError;
		std::string errorCategory;
		bool timedOut = false;
		bool aborted = false;
		bool skipDeliverySimulation = false;
		bool skipPrimaryDeliverySimulation = false;
		bool skipFailureDestinationSimulation = false;
		bool runtimeProjectedTransport = false;
		bool runtimeProjectedPrimaryTransport = false;
		bool runtimeProjectedFailureDestinationTransport = false;
		bool hasRetryDelayOverride = false;
		std::int64_t retryDelayOverrideMs = 0;
	};

	void ApplyRuntimeExecutionResult(
		CronTimerRunOutcome& outcome,
		const CronJson& runtimeResult);

} // namespace blazeclaw::cron
