#pragma once

#include "GatewayProtocolModels.h"
#include "GatewayProtocolCodec.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayRequestPolicyGuard.h"

#include <string>
#include <optional>
#include <functional>

namespace blazeclaw::gateway {

	class GatewayHost;

	/// Coordinator for protocol ingress pipeline stages.
	/// Extracts decode, normalize, schema-validate, policy-guard, dispatch, and response-validate
	/// into explicit pipeline stages with telemetry and error handling.
	///
	/// This is a thin collaborator following the Workstream A extraction pattern:
	/// - GatewayHost retains ingress ownership and composition root responsibilities
	/// - Pipeline stages read host state through injected context/callbacks
	/// - Wire-format, error shapes, telemetry names, and stage ordering frozen per Step 2
	namespace GatewayHostProtocolIngressPipeline {

		/// Context for pipeline stages providing host state accessors and routing callbacks.
		/// Pipeline stages read but do not mutate host composition-root state directly.
		struct PipelineContext {
			/// Request policy guard for evaluating request policy before dispatch
			const GatewayRequestPolicyGuard* requestPolicyGuard = nullptr;

			/// Host dispatch initialization state for policy context
			bool dispatchInitialized = false;

			/// Host running state for policy context
			bool hostRunning = false;

			/// Routing callback: delegates to GatewayHost::RouteRequest
			/// Signature: const protocol::RequestFrame& -> protocol::ResponseFrame
			std::function<protocol::ResponseFrame(const protocol::RequestFrame&)> routeRequest;

			/// Telemetry emission callback: delegates to EmitTelemetryEvent
			/// Signature: (eventName, payloadJson) -> void
			std::function<void(const std::string&, const std::string&)> emitTelemetry;
		};

		/// Execute the full protocol ingress pipeline on an inbound text frame.
		///
		/// Pipeline stages (locked order per Step 2):
		/// 1. Decode: TryDecodeRequestFrame with invalid_frame error handling
		/// 2. Normalize: Method-specific pre-validation normalization (cron params)
		/// 3. Schema Validate: Request schema validation with method-specific telemetry
		/// 4. Policy Guard: Request policy evaluation with host runtime context
		/// 5. Dispatch: Route decision and request execution via context.routeRequest
		/// 6. Response Validate: Response schema validation for method contract compliance
		///
		/// Wire-format and error-shape behavior remains unchanged from prior monolithic implementation.
		///
		/// @param inboundJson Raw inbound JSON text frame
		/// @param context Pipeline context with host state accessors and routing callbacks
		/// @return Encoded response frame (success or error)
		[[nodiscard]] std::string ExecuteInboundTextPipeline(
			const std::string& inboundJson,
			const PipelineContext& context);

		/// Pre-validation normalization for method-specific param compatibility.
		/// Currently handles cron.add, cron.update, cron.remove, cron.run, cron.runs, wake methods.
		///
		/// This helper is extracted from GatewayHost.cpp anonymous namespace for reuse.
		///
		/// @param method Request method name
		/// @param paramsJson Original request params JSON
		/// @return Normalized params JSON if changes were applied, std::nullopt otherwise
		[[nodiscard]] std::optional<std::string> TryNormalizeCronParamsPreValidation(
			const std::string& method,
			const std::optional<std::string>& paramsJson);

	} // namespace GatewayHostProtocolIngressPipeline

} // namespace blazeclaw::gateway
