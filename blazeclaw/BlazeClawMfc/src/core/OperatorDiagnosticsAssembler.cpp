#include "pch.h"
#include "OperatorDiagnosticsAssembler.h"

namespace blazeclaw::core {

	namespace {

		const char* FeatureStateLabel(const FeatureState state) {
			switch (state) {
			case FeatureState::Implemented:
				return "implemented";
			case FeatureState::InProgress:
				return "in_progress";
			case FeatureState::Planned:
			default:
				return "planned";
			}
		}

	} // namespace

	OperatorDiagnosticsAssembler::OperatorDiagnosticsAssembler(
		GatewayLifecycleDiagnosticsProjector& gatewayLifecycle,
		EmailRuntimeDiagnosticsProjector& email,
		EmbeddedRuntimeDiagnosticsProjector& embedded,
		ModelRuntimeDiagnosticsProjector& modelRuntime,
		HooksDiagnosticsProjector& hooks,
		CDiagnosticsReportBuilder& reportBuilder)
		: m_gatewayLifecycle(gatewayLifecycle)
		, m_email(email)
		, m_embedded(embedded)
		, m_modelRuntime(modelRuntime)
		, m_hooks(hooks)
		, m_reportBuilder(reportBuilder) {
	}

	std::string OperatorDiagnosticsAssembler::Build(
		const OperatorDiagnosticsInputs& inputs) const {
		DiagnosticsSnapshot snapshot;

		m_gatewayLifecycle.Apply(inputs.gatewayLifecycle, snapshot);

		std::size_t implementedCount = 0;
		std::size_t inProgressCount = 0;
		std::size_t plannedCount = 0;
		if (inputs.featureRegistry != nullptr) {
			for (const auto& feature : inputs.featureRegistry->Features()) {
				if (feature.state == FeatureState::Implemented) {
					++implementedCount;
					continue;
				}

				if (feature.state == FeatureState::InProgress) {
					++inProgressCount;
					continue;
				}

				++plannedCount;
			}
		}

		m_email.Apply(inputs.email, snapshot);

		snapshot.agentsCount = inputs.agentsCount;
		snapshot.agentsDefaultAgent = inputs.agentsDefaultAgent;
		snapshot.subagentsActive = inputs.subagentsActive;
		snapshot.subagentsPendingAnnounce = inputs.subagentsPendingAnnounce;
		snapshot.acpLastAllowed = inputs.acpLastAllowed;
		snapshot.acpReason = inputs.acpReason;

		m_embedded.Apply(inputs.embedded, snapshot);

		snapshot.toolsPolicyEntries = inputs.toolsPolicyEntries;
		snapshot.toolsShellProcesses = inputs.toolsShellProcesses;

		snapshot.modelPrimary = inputs.modelPrimary;
		snapshot.modelFallback = inputs.modelFallback;
		snapshot.modelFailovers = inputs.modelFailovers;
		snapshot.authProfiles = inputs.authProfiles;

		snapshot.sandboxEnabledCount = inputs.sandboxEnabledCount;
		snapshot.sandboxBrowserEnabledCount = inputs.sandboxBrowserEnabledCount;

		m_modelRuntime.Apply(inputs.modelRuntime, snapshot);

		snapshot.skillsCatalogEntries = inputs.skillsCatalogEntries;
		snapshot.skillsPromptIncluded = inputs.skillsPromptIncluded;
		if (inputs.skillsPromptWide != nullptr) {
			snapshot.skillsSelfEvolvingReminderInjected =
				inputs.skillsPromptWide->find(L"## Self-Evolving Reminder") !=
				std::wstring::npos;
		}

		m_hooks.Apply(inputs.hooks, snapshot);

		snapshot.featuresImplemented = implementedCount;
		snapshot.featuresInProgress = inProgressCount;
		snapshot.featuresPlanned = plannedCount;
		snapshot.featuresRegistryState = FeatureStateLabel(
			inputs.featureRegistry == nullptr ||
			inputs.featureRegistry->Features().empty()
			? FeatureState::Planned
			: inputs.featureRegistry->Features().front().state);

		return m_reportBuilder.BuildOperatorDiagnosticsReport(snapshot);
	}

} // namespace blazeclaw::core
