#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

	void GatewayHost::RegisterEventHandlers() {
		m_dispatcher.Register("gateway.events.batch", [this](const protocol::RequestFrame& request) {
			const auto recipients = m_transportRecipientRegistry.GetSnapshot();
			return protocol::OkResponse(request, "{\"batches\":[\"lifecycle\",\"updates\",\"chat.lifecycle\"],\"count\":3"
				",\"activeSubscribers\":" + std::to_string(recipients.recipientCount) +
				",\"finalizedRuns\":" + std::to_string(recipients.finalizedRunCount) + "}");
			});

		m_dispatcher.Register("gateway.events.cursor", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursor\":\"evt-" + std::to_string(m_chatPushEventSeq) +
				"\",\"event\":\"chat.lifecycle\",\"sequence\":" +
				std::to_string(m_chatPushEventSeq) + "}");
			});

		m_dispatcher.Register("gateway.events.anchor", [this](const protocol::RequestFrame& request) {
			const std::uint64_t anchorSeq =
				m_chatPushEventSeq > 32 ? m_chatPushEventSeq - 32 : 0;
			return protocol::OkResponse(request, "{\"anchor\":\"evt-" + std::to_string(anchorSeq) +
				"\",\"event\":\"chat.lifecycle\",\"sequence\":" +
				std::to_string(anchorSeq) + "}");
			});

		m_dispatcher.Register("gateway.events.offset", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offset\":" +
					std::to_string(m_chatEventsBySession.size()) +
					",\"event\":\"chat.lifecycle\"}");
			});

		m_dispatcher.Register("gateway.events.marker", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"marker\":\"evt-marker-" +
					std::to_string(m_chatPushEventSeq) +
					"\",\"event\":\"chat.lifecycle\"}");
			});

		m_dispatcher.Register("gateway.events.sequence", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequence\":" +
					std::to_string(m_chatPushEventSeq) +
					",\"event\":\"chat.lifecycle\"}");
			});

		m_dispatcher.Register("gateway.events.pointer", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointer\":\"evt-pointer-" +
					std::to_string(m_chatPushEventSeq) +
					"\",\"event\":\"chat.lifecycle\"}");
			});

		m_dispatcher.Register("gateway.events.token", [this](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"token\":\"evt-token-" +
					std::to_string(m_chatPushEventSeq) +
					"\",\"event\":\"chat.lifecycle\"}");
			});

		m_dispatcher.Register("gateway.events.stream", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stream\":\"evt-stream-1\",\"event\":\"gateway.tools.catalog.update\"}");
			});

		m_dispatcher.Register("gateway.events.windowId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowId\":\"evt-window-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.sessionKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sessionKey\":\"sess-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.contextKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"contextKey\":\"context-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.channelKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"channelKey\":\"channel-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.routeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"routeKey\":\"route-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.accountKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"accountKey\":\"account-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.agentKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"agentKey\":\"agent-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.modelKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"modelKey\":\"model-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.configKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"configKey\":\"config-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.policyKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"policyKey\":\"policy-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.toolKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"toolKey\":\"tool-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.transportKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"transportKey\":\"transport-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.runtimeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"runtimeKey\":\"runtime-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"state-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.healthKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthKey\":\"health-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.logKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"logKey\":\"log-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.metricKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"metricKey\":\"metric-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.traceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"traceKey\":\"trace-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.auditKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"auditKey\":\"audit-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.debugKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"debugKey\":\"debug-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.cacheKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cacheKey\":\"cache-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.queueKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"queueKey\":\"queue-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"window-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.cursorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorKey\":\"cursor-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.anchorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorKey\":\"anchor-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.offsetKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetKey\":\"offset-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.markerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerKey\":\"marker-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.pointerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerKey\":\"pointer-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"token-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.sequenceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceKey\":\"sequence-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.streamKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamKey\":\"stream-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.bundleKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleKey\":\"bundle-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.packageKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageKey\":\"package-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.archiveKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveKey\":\"archive-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.manifestKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"manifestKey\":\"manifest-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.profileKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profileKey\":\"profile-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.templateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"templateKey\":\"template-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.revisionKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"revisionKey\":\"revision-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.historyKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"historyKey\":\"history-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.snapshotKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotKey\":\"snapshot-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.indexKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"indexKey\":\"index-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"window-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"cursor-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.anchorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeKey\":\"anchor-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.offsetScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeKey\":\"offset-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.pointerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeKey\":\"pointer-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"token-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.streamScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeKey\":\"stream-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.sequenceScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeKey\":\"sequence-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.bundleScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeKey\":\"bundle-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.packageScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageScopeKey\":\"package-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.archiveScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveScopeKey\":\"archive-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.manifestScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"manifestScopeKey\":\"manifest-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.profileScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profileScopeKey\":\"profile-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.templateScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"templateScopeKey\":\"template-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.revisionScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"revisionScopeKey\":\"revision-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.historyScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"historyScopeKey\":\"history-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.snapshotScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotScopeKey\":\"snapshot-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.indexScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"indexScopeKey\":\"index-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"window-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"cursor-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.anchorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeId\":\"anchor-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.offsetScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeId\":\"offset-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.pointerScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeId\":\"pointer-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.tokenScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeId\":\"token-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.sequenceScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeId\":\"sequence-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.streamScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeId\":\"stream-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.bundleScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeId\":\"bundle-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.packageScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageScopeId\":\"package-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.archiveScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveScopeId\":\"archive-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.manifestScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"manifestScopeId\":\"manifest-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.profileScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profileScopeId\":\"profile-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.templateScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"templateScopeId\":\"template-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.revisionScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"revisionScopeId\":\"revision-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.historyScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"historyScopeId\":\"history-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.snapshotScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotScopeId\":\"snapshot-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.indexScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"indexScopeId\":\"index-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.markerScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerScopeId\":\"marker-scope-id-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.markerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerScopeKey\":\"marker-scope-key-1\",\"event\":\"gateway.session.reset\"}");
			});

		m_dispatcher.Register("gateway.events.recent", [this](const protocol::RequestFrame& request) {
			const auto recipients = m_transportRecipientRegistry.GetSnapshot();
			return protocol::OkResponse(request, "{\"events\":[\"chat.lifecycle\",\"gateway.session.reset\"],\"count\":2"
					",\"sequenceWatermark\":" + std::to_string(m_chatPushEventSeq) +
					",\"activeSubscribers\":" + std::to_string(recipients.recipientCount) +
					",\"activeRuns\":" + std::to_string(m_chatRunsById.size()) + "}");
			});
	}

} // namespace blazeclaw::gateway
