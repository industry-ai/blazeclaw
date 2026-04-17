#pragma once

#include "../config/ConfigModels.h"

#include <cstdint>
#include <string>
#include <vector>

namespace blazeclaw::core {

	class EmailPolicyOrchestrationService {
	public:
		struct ResolvedEmailFallbackPolicy {
			std::wstring profileId;
			std::vector<std::wstring> backends;
			std::wstring onUnavailable;
			std::wstring onAuthError;
			std::wstring onExecError;
			std::uint32_t retryMaxAttempts = 1;
			std::uint32_t retryDelayMs = 0;
			bool requiresApproval = true;
			std::uint32_t approvalTokenTtlMinutes = 60;
		};

		struct GatewayEmailPolicyBinding {
			bool preflightEnabled = false;
			bool runtimeEnabled = false;
			bool runtimeEnforce = false;
			std::vector<std::string> backends;
			std::string onUnavailable;
			std::string onAuthError;
			std::string onExecError;
			std::uint32_t retryMaxAttempts = 1;
			std::uint32_t retryDelayMs = 0;
			bool requiresApproval = true;
			std::uint32_t approvalTokenTtlMinutes = 60;
			std::string profileId = "legacy-policy";
		};

		[[nodiscard]] ResolvedEmailFallbackPolicy ResolveFallbackPolicy(
			const blazeclaw::config::AppConfig& config,
			const std::wstring& toolName,
			const std::wstring& capabilityName) const {
			const auto resolvedPolicy = blazeclaw::config::ResolveEmailFallbackPolicy(
				config.email.policy,
				toolName,
				capabilityName);

			return ResolvedEmailFallbackPolicy{
				.profileId = resolvedPolicy.profileId,
				.backends = resolvedPolicy.backends,
				.onUnavailable = resolvedPolicy.onUnavailable,
				.onAuthError = resolvedPolicy.onAuthError,
				.onExecError = resolvedPolicy.onExecError,
				.retryMaxAttempts = resolvedPolicy.retryMaxAttempts,
				.retryDelayMs = resolvedPolicy.retryDelayMs,
				.requiresApproval = resolvedPolicy.requiresApproval,
				.approvalTokenTtlMinutes = resolvedPolicy.approvalTokenTtlMinutes,
			};
		}

		[[nodiscard]] GatewayEmailPolicyBinding BuildGatewayPolicyBinding(
			const blazeclaw::config::EmailFallbackConfig& emailConfig,
			const bool runtimeEnabled,
			const bool runtimeEnforce,
			const ResolvedEmailFallbackPolicy& resolvedPolicy) const {
			GatewayEmailPolicyBinding binding;
			binding.preflightEnabled = emailConfig.preflight.enabled;
			binding.runtimeEnabled = runtimeEnabled;
			binding.runtimeEnforce = runtimeEnforce;
			binding.backends.reserve(resolvedPolicy.backends.size());
			for (const auto& backend : resolvedPolicy.backends) {
				binding.backends.push_back(ToNarrowAscii(backend));
			}

			binding.onUnavailable = ToNarrowAscii(resolvedPolicy.onUnavailable);
			binding.onAuthError = ToNarrowAscii(resolvedPolicy.onAuthError);
			binding.onExecError = ToNarrowAscii(resolvedPolicy.onExecError);
			binding.retryMaxAttempts = runtimeEnabled
				? resolvedPolicy.retryMaxAttempts
				: std::uint32_t{ 1 };
			binding.retryDelayMs = runtimeEnabled
				? resolvedPolicy.retryDelayMs
				: std::uint32_t{ 0 };
			binding.requiresApproval = resolvedPolicy.requiresApproval;
			binding.approvalTokenTtlMinutes =
				resolvedPolicy.approvalTokenTtlMinutes;
			binding.profileId = runtimeEnabled
				? ToNarrowAscii(resolvedPolicy.profileId)
				: std::string("legacy-policy");
			return binding;
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
