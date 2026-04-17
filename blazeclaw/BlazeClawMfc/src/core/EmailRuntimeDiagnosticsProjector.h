#pragma once

#include "EmailPolicyOrchestrationService.h"
#include "../config/ConfigModels.h"
#include "diagnostics/DiagnosticsSnapshot.h"
#include "../gateway/executors/EmailScheduleExecutor.h"

#include <cstdint>
#include <string>

namespace blazeclaw::core {

	class EmailRuntimeDiagnosticsProjector {
	public:
		struct Context {
			const blazeclaw::config::EmailFallbackConfig& emailConfig;
			std::wstring policyRolloutMode;
			std::wstring policyEnforceChannel;
			bool policyCanaryEligible = false;
			bool rollbackBridgeEnabled = false;
			bool runtimeEnabled = false;
			bool runtimeEnforce = false;
			const EmailPolicyOrchestrationService::ResolvedEmailFallbackPolicy&
				resolvedPolicy;
			const blazeclaw::gateway::executors::RuntimeHealthIndex& healthIndex;
			std::uint64_t fallbackAttempts = 0;
			std::uint64_t fallbackSuccess = 0;
			std::uint64_t fallbackFailure = 0;
		};

		void Apply(
			const Context& context,
			DiagnosticsSnapshot& snapshot) const {
			snapshot.emailPreflightEnabled =
				context.emailConfig.preflight.enabled;
			snapshot.emailPolicyProfilesEnabled =
				context.emailConfig.policyProfiles.enabled;
			snapshot.emailPolicyProfilesEnforce =
				context.emailConfig.policyProfiles.enforce;
			snapshot.emailPolicyProfilesRuntimeEnabled =
				context.runtimeEnabled;
			snapshot.emailPolicyProfilesRuntimeEnforce =
				context.runtimeEnforce;
			snapshot.emailPolicyRolloutMode =
				ToNarrowAscii(context.policyRolloutMode);
			snapshot.emailPolicyEnforceChannel =
				ToNarrowAscii(context.policyEnforceChannel);
			snapshot.emailPolicyCanaryEligible =
				context.policyCanaryEligible;
			snapshot.emailRollbackBridgeEnabled =
				context.rollbackBridgeEnabled;
			snapshot.emailResolvedPolicyId =
				ToNarrowAscii(context.resolvedPolicy.profileId);
			snapshot.emailResolvedBackends.clear();
			snapshot.emailResolvedBackends.reserve(context.resolvedPolicy.backends.size());
			for (const auto& backend : context.resolvedPolicy.backends) {
				snapshot.emailResolvedBackends.push_back(ToNarrowAscii(backend));
			}
			snapshot.emailPolicyActionUnavailable =
				ToNarrowAscii(context.resolvedPolicy.onUnavailable);
			snapshot.emailPolicyActionAuthError =
				ToNarrowAscii(context.resolvedPolicy.onAuthError);
			snapshot.emailPolicyActionExecError =
				ToNarrowAscii(context.resolvedPolicy.onExecError);
			snapshot.emailRetryMaxAttempts =
				context.resolvedPolicy.retryMaxAttempts;
			snapshot.emailRetryDelayMs = context.resolvedPolicy.retryDelayMs;
			snapshot.emailRequiresApproval = context.resolvedPolicy.requiresApproval;
			snapshot.emailApprovalTokenTtlMinutes =
				context.resolvedPolicy.approvalTokenTtlMinutes;
			snapshot.emailCapabilityState = context.healthIndex.emailSendState;
			snapshot.emailHealthGeneratedAtEpochMs =
				context.healthIndex.generatedAtEpochMs;
			snapshot.emailHealthTtlMs = context.healthIndex.ttlMs;
			snapshot.emailProbeReadyCount = 0;
			snapshot.emailProbeUnavailableCount = 0;
			for (const auto& probe : context.healthIndex.probes) {
				if (probe.state == "ready") {
					++snapshot.emailProbeReadyCount;
					continue;
				}

				if (probe.state == "unavailable") {
					++snapshot.emailProbeUnavailableCount;
				}
			}

			snapshot.emailFallbackAttempts = context.fallbackAttempts;
			snapshot.emailFallbackSuccess = context.fallbackSuccess;
			snapshot.emailFallbackFailure = context.fallbackFailure;
		}

	private:
		[[nodiscard]] static std::string ToNarrowAscii(
			const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}
	};

} // namespace blazeclaw::core
