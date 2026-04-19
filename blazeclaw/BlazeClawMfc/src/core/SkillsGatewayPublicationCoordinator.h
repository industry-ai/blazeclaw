#pragma once

namespace blazeclaw::core {

class ServiceManager;

/// Owns gateway skills catalog projection refresh/publish steps (orchestration around cached
/// `m_gatewaySkillsStateProjection`), keeping `ServiceManager` entry points thin.
class SkillsGatewayPublicationCoordinator {
public:
	static void RefreshProjection(ServiceManager& manager);
	static void PublishProjection(ServiceManager& manager);
};

} // namespace blazeclaw::core
