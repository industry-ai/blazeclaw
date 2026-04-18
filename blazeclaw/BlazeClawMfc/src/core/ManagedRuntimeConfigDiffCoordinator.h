#pragma once

#include "../config/ConfigModels.h"

#include <functional>
#include <optional>

namespace blazeclaw::core {

	class ManagedRuntimeConfigDiffCoordinator {
	public:
		struct AuthGuardResult {
			bool accepted = true;
			std::uint64_t requiredGeneration = 0;
			std::wstring warningMessage;
		};

		struct LocalModelReloadResult {
			blazeclaw::config::AppConfig updatedConfig;
			bool activationEnabled = false;
			bool rolloutEligible = false;
			std::string activationReason;
			std::wstring warningMessage;
		};

		[[nodiscard]] AuthGuardResult EvaluateAuthSessionGenerationGuard(
			const blazeclaw::config::AppConfig& currentConfig,
			const blazeclaw::config::AppConfig& nextConfig,
			const bool authSensitiveChanged,
			const std::uint64_t currentGeneration) const {
			if (!authSensitiveChanged ||
				nextConfig.gateway.authSessionGeneration > currentGeneration) {
				return AuthGuardResult{
					.accepted = true,
					.requiredGeneration = nextConfig.gateway.authSessionGeneration,
					.warningMessage = L"",
				};
			}

			return AuthGuardResult{
				.accepted = false,
				.requiredGeneration = currentGeneration + 1,
				.warningMessage =
					L"gateway auth/session config change requires "
					L"gateway.authSessionGeneration to increase.",
			};
		}

		[[nodiscard]] LocalModelReloadResult CoordinateLocalModelReload(
			const blazeclaw::config::AppConfig& currentConfig,
			const blazeclaw::config::AppConfig& nextConfig,
			const bool localModelActivationEnabled,
			const std::function<bool(const blazeclaw::config::AppConfig&)>&
			isRolloutEligible,
			const std::function<bool(blazeclaw::config::AppConfig&)>&
			activateRuntime) const {
			LocalModelReloadResult result;
			result.updatedConfig = currentConfig;
			result.updatedConfig.localModel = nextConfig.localModel;

			const bool rolloutEligible =
				isRolloutEligible ? isRolloutEligible(result.updatedConfig) : true;
			result.rolloutEligible = rolloutEligible;

			if (!result.updatedConfig.localModel.enabled) {
				result.activationEnabled = false;
				result.activationReason = "config_disabled";
				return result;
			}

			if (!rolloutEligible) {
				result.activationEnabled = false;
				result.activationReason = "rollout_stage_not_eligible";
				return result;
			}

			const bool activated = activateRuntime
				? activateRuntime(result.updatedConfig)
				: false;
			result.activationEnabled = activated;
			result.activationReason = activated
				? "active"
				: "initialization_failed";

			if (nextConfig.localModel.enabled &&
				!activated &&
				localModelActivationEnabled) {
				result.warningMessage =
					L"local model activation failed for reloaded config; reverted "
					L"to last known-good local model runtime settings.";
			}

			return result;
		}
	};

} // namespace blazeclaw::core
