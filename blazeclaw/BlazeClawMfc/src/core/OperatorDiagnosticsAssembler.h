#pragma once

#include "diagnostics/CDiagnosticsReportBuilder.h"
#include "FeatureRegistry.h"
#include "diagnostics/DiagnosticsSnapshot.h"
#include "EmbeddedRuntimeDiagnosticsProjector.h"
#include "EmailRuntimeDiagnosticsProjector.h"
#include "FeatureRegistry.h"
#include "GatewayLifecycleDiagnosticsProjector.h"
#include "HooksDiagnosticsProjector.h"
#include "ModelRuntimeDiagnosticsProjector.h"

#include <string>

namespace blazeclaw::core {

	/// Inputs for a full operator diagnostics report (projector contexts + scalar fields
	/// that are not yet covered by a dedicated projector).
	struct OperatorDiagnosticsInputs {
		GatewayLifecycleDiagnosticsProjector::Context gatewayLifecycle;
		EmailRuntimeDiagnosticsProjector::Context email;
		EmbeddedRuntimeDiagnosticsProjector::Context embedded;
		ModelRuntimeDiagnosticsProjector::Context modelRuntime;
		HooksDiagnosticsProjector::Context hooks;

		const FeatureRegistry* featureRegistry = nullptr;

		std::size_t agentsCount = 0;
		std::string agentsDefaultAgent;
		std::size_t subagentsActive = 0;
		std::size_t subagentsPendingAnnounce = 0;
		bool acpLastAllowed = false;
		std::string acpReason;

		std::size_t toolsPolicyEntries = 0;
		std::size_t toolsShellProcesses = 0;

		std::string modelPrimary;
		std::string modelFallback;
		std::size_t modelFailovers = 0;
		std::size_t authProfiles = 0;

		std::size_t sandboxEnabledCount = 0;
		std::size_t sandboxBrowserEnabledCount = 0;

		std::size_t skillsCatalogEntries = 0;
		std::size_t skillsPromptIncluded = 0;
		const std::wstring* skillsPromptWide = nullptr;
	};

	/// Assembles `DiagnosticsSnapshot` from projector contexts and scalar fields, then
	/// renders the operator report string via `CDiagnosticsReportBuilder`.
	class OperatorDiagnosticsAssembler {
	public:
		OperatorDiagnosticsAssembler(
			GatewayLifecycleDiagnosticsProjector& gatewayLifecycle,
			EmailRuntimeDiagnosticsProjector& email,
			EmbeddedRuntimeDiagnosticsProjector& embedded,
			ModelRuntimeDiagnosticsProjector& modelRuntime,
			HooksDiagnosticsProjector& hooks,
			CDiagnosticsReportBuilder& reportBuilder);

		[[nodiscard]] std::string Build(const OperatorDiagnosticsInputs& inputs) const;

	private:
		GatewayLifecycleDiagnosticsProjector& m_gatewayLifecycle;
		EmailRuntimeDiagnosticsProjector& m_email;
		EmbeddedRuntimeDiagnosticsProjector& m_embedded;
		ModelRuntimeDiagnosticsProjector& m_modelRuntime;
		HooksDiagnosticsProjector& m_hooks;
		CDiagnosticsReportBuilder& m_reportBuilder;
	};

} // namespace blazeclaw::core
