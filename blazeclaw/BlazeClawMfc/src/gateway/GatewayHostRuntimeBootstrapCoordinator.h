#pragma once

#include <string>

namespace blazeclaw::gateway {

	class GatewayHost;
	class GatewayToolRegistry;

	/// Workstream A Step 3: explicit runtime bootstrap stages extracted from
	/// `GatewayHost::StartRuntimeServices`. `GatewayHost` remains the composition root;
	/// this coordinator owns startup wiring order and telemetry for extension/skill/runtime
	/// activation while preserving the staged bootstrap sequence documented in
	/// `docs/reviews/WORKSTREAM_A_STEP2_EXTRACTION_BOUNDARIES.md`.
	namespace GatewayHostRuntimeBootstrap {

		struct Access;

		[[nodiscard]] std::string ResolveExtensionsCatalogPath();
		void EnsureOpsToolsRuntimeRegistered(GatewayToolRegistry& registry);
		void EnsurePdfGeneratorRuntimeRegistered(GatewayToolRegistry& registry);

		[[nodiscard]] bool RunStartRuntimeServices(GatewayHost& host);

	} // namespace GatewayHostRuntimeBootstrap

} // namespace blazeclaw::gateway
