#include "pch.h"
#include "SkillsGatewayPublicationCoordinator.h"

#include "ServiceManager.h"

namespace blazeclaw::core {

void SkillsGatewayPublicationCoordinator::RefreshProjection(ServiceManager& manager) {
	if (manager.m_extensionBundleCommandSourceAdapter) {
		const auto& diagnostics =
			manager.m_extensionBundleCommandSourceAdapter->LastDiagnostics();
		manager.m_bundleCommandRootsScannedCount = diagnostics.rootsScanned;
		manager.m_bundleCommandFilesLoadedCount = diagnostics.filesLoaded;
		manager.m_bundleCommandFilesSkippedDisabledCount =
			diagnostics.filesSkippedDisabled;
		manager.m_bundleCommandFilesSkippedEmptyPromptCount =
			diagnostics.filesSkippedEmptyPrompt;
		manager.m_bundleCommandFilesSkippedInvalidNameCount =
			diagnostics.filesSkippedInvalidName;
		manager.m_bundleCommandFilesRejectedUnsafeCount =
			diagnostics.filesRejectedUnsafe;
	}

	manager.m_gatewaySkillsStateProjection = manager.BuildGatewaySkillsState();
	manager.m_gatewaySkillsStateProjection.bundleCommandRootsScannedCount =
		manager.m_bundleCommandRootsScannedCount;
	manager.m_gatewaySkillsStateProjection.bundleCommandFilesLoadedCount =
		manager.m_bundleCommandFilesLoadedCount;
	manager.m_gatewaySkillsStateProjection.bundleCommandFilesSkippedDisabledCount =
		manager.m_bundleCommandFilesSkippedDisabledCount;
	manager.m_gatewaySkillsStateProjection.bundleCommandFilesSkippedEmptyPromptCount =
		manager.m_bundleCommandFilesSkippedEmptyPromptCount;
	manager.m_gatewaySkillsStateProjection.bundleCommandFilesSkippedInvalidNameCount =
		manager.m_bundleCommandFilesSkippedInvalidNameCount;
	manager.m_gatewaySkillsStateProjection.bundleCommandFilesRejectedUnsafeCount =
		manager.m_bundleCommandFilesRejectedUnsafeCount;
}

void SkillsGatewayPublicationCoordinator::PublishProjection(ServiceManager& manager) {
	manager.m_gatewayHost.SetSkillsCatalogState(manager.m_gatewaySkillsStateProjection);
}

} // namespace blazeclaw::core
