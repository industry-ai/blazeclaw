#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Included inside namespace blazeclaw::gateway::runtime_local (see
// GatewayHostRuntimeLocalHelpers.h / GatewayHostRuntimeLocalHelpers.cpp).

struct ChatPromptOrchestrationResult {
	bool matched = false;
	bool success = false;
	bool requiresApproval = false;
	std::string terminalStatus;
	std::string terminalReason;
	std::string fallbackBackend;
	std::string fallbackAction;
	std::size_t fallbackAttempt = 0;
	std::size_t fallbackMaxAttempts = 0;
	std::string assistantText;
	std::vector<std::string> assistantDeltas;
	std::string errorCode;
	std::string errorMessage;
	std::string approvalToken;
	std::uint64_t approvalTokenExpiresAtEpochMs = 0;
	std::string approvalNextAction;
	std::string approvalPrompt;
	std::vector<std::string> missReasons;
	std::string city;
	std::string date;
	std::string recipient;
	std::string sendAt;
	std::string scheduleKind;
	std::size_t decompositionSteps = 0;
};

struct PromptScheduleResolution {
	bool hasSchedule = false;
	bool immediate = false;
	std::string sendAt;
	std::string kind;
};

struct OrderedSequencePreflight {
	bool enforced = false;
	bool strictAllowlist = false;
	std::vector<std::string> orderedTargets;
	std::vector<std::string> explicitCallTargets;
	std::vector<std::string> resolvedToolTargets;
	std::vector<std::string> missingTargets;
	std::vector<std::string> missingResolvedToolTargets;
};
