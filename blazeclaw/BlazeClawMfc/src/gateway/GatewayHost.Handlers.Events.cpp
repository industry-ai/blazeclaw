#include "pch.h"
#include "GatewayHost.h"
#include "GatewayStaticRegistration.h"

namespace blazeclaw::gateway {
	namespace {
		static constexpr StaticPayloadHandlerEntry kEventsStaticPayloadHandlers[] = {
			{ "gateway.events.stream", "{\"stream\":\"evt-stream-1\",\"event\":\"gateway.tools.catalog.update\"}" },
			{ "gateway.events.windowId", "{\"windowId\":\"evt-window-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.sessionKey", "{\"sessionKey\":\"sess-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.scopeKey", "{\"scopeKey\":\"scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.contextKey", "{\"contextKey\":\"context-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.channelKey", "{\"channelKey\":\"channel-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.routeKey", "{\"routeKey\":\"route-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.accountKey", "{\"accountKey\":\"account-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.agentKey", "{\"agentKey\":\"agent-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.modelKey", "{\"modelKey\":\"model-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.configKey", "{\"configKey\":\"config-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.policyKey", "{\"policyKey\":\"policy-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.toolKey", "{\"toolKey\":\"tool-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.transportKey", "{\"transportKey\":\"transport-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.runtimeKey", "{\"runtimeKey\":\"runtime-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.stateKey", "{\"stateKey\":\"state-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.healthKey", "{\"healthKey\":\"health-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.logKey", "{\"logKey\":\"log-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.metricKey", "{\"metricKey\":\"metric-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.traceKey", "{\"traceKey\":\"trace-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.auditKey", "{\"auditKey\":\"audit-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.debugKey", "{\"debugKey\":\"debug-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.cacheKey", "{\"cacheKey\":\"cache-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.queueKey", "{\"queueKey\":\"queue-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.windowKey", "{\"windowKey\":\"window-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.cursorKey", "{\"cursorKey\":\"cursor-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.anchorKey", "{\"anchorKey\":\"anchor-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.offsetKey", "{\"offsetKey\":\"offset-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.markerKey", "{\"markerKey\":\"marker-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.pointerKey", "{\"pointerKey\":\"pointer-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.tokenKey", "{\"tokenKey\":\"token-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.sequenceKey", "{\"sequenceKey\":\"sequence-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.streamKey", "{\"streamKey\":\"stream-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.bundleKey", "{\"bundleKey\":\"bundle-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.packageKey", "{\"packageKey\":\"package-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.archiveKey", "{\"archiveKey\":\"archive-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.manifestKey", "{\"manifestKey\":\"manifest-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.profileKey", "{\"profileKey\":\"profile-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.templateKey", "{\"templateKey\":\"template-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.revisionKey", "{\"revisionKey\":\"revision-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.historyKey", "{\"historyKey\":\"history-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.snapshotKey", "{\"snapshotKey\":\"snapshot-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.indexKey", "{\"indexKey\":\"index-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.windowScopeKey", "{\"windowScopeKey\":\"window-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.cursorScopeKey", "{\"cursorScopeKey\":\"cursor-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.anchorScopeKey", "{\"anchorScopeKey\":\"anchor-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.offsetScopeKey", "{\"offsetScopeKey\":\"offset-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.pointerScopeKey", "{\"pointerScopeKey\":\"pointer-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.tokenScopeKey", "{\"tokenScopeKey\":\"token-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.streamScopeKey", "{\"streamScopeKey\":\"stream-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.sequenceScopeKey", "{\"sequenceScopeKey\":\"sequence-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.bundleScopeKey", "{\"bundleScopeKey\":\"bundle-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.packageScopeKey", "{\"packageScopeKey\":\"package-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.archiveScopeKey", "{\"archiveScopeKey\":\"archive-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.manifestScopeKey", "{\"manifestScopeKey\":\"manifest-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.profileScopeKey", "{\"profileScopeKey\":\"profile-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.templateScopeKey", "{\"templateScopeKey\":\"template-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.revisionScopeKey", "{\"revisionScopeKey\":\"revision-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.historyScopeKey", "{\"historyScopeKey\":\"history-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.snapshotScopeKey", "{\"snapshotScopeKey\":\"snapshot-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.indexScopeKey", "{\"indexScopeKey\":\"index-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.windowScopeId", "{\"windowScopeId\":\"window-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.cursorScopeId", "{\"cursorScopeId\":\"cursor-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.anchorScopeId", "{\"anchorScopeId\":\"anchor-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.offsetScopeId", "{\"offsetScopeId\":\"offset-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.pointerScopeId", "{\"pointerScopeId\":\"pointer-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.tokenScopeId", "{\"tokenScopeId\":\"token-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.sequenceScopeId", "{\"sequenceScopeId\":\"sequence-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.streamScopeId", "{\"streamScopeId\":\"stream-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.bundleScopeId", "{\"bundleScopeId\":\"bundle-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.packageScopeId", "{\"packageScopeId\":\"package-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.archiveScopeId", "{\"archiveScopeId\":\"archive-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.manifestScopeId", "{\"manifestScopeId\":\"manifest-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.profileScopeId", "{\"profileScopeId\":\"profile-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.templateScopeId", "{\"templateScopeId\":\"template-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.revisionScopeId", "{\"revisionScopeId\":\"revision-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.historyScopeId", "{\"historyScopeId\":\"history-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.snapshotScopeId", "{\"snapshotScopeId\":\"snapshot-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.indexScopeId", "{\"indexScopeId\":\"index-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.markerScopeId", "{\"markerScopeId\":\"marker-scope-id-1\",\"event\":\"gateway.session.reset\"}" },
			{ "gateway.events.markerScopeKey", "{\"markerScopeKey\":\"marker-scope-key-1\",\"event\":\"gateway.session.reset\"}" },
		};
	}

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

		RegisterStaticPayloadHandlers(
			m_dispatcher,
			kEventsStaticPayloadHandlers,
			sizeof(kEventsStaticPayloadHandlers) / sizeof(kEventsStaticPayloadHandlers[0]));

		m_dispatcher.Register("gateway.events.recent", [this](const protocol::RequestFrame& request) {
			const auto recipients = m_transportRecipientRegistry.GetSnapshot();
			return protocol::OkResponse(request, "{\"events\":[\"chat.lifecycle\",\"gateway.session.reset\"],\"count\":2"
					",\"sequenceWatermark\":" + std::to_string(m_chatPushEventSeq) +
					",\"activeSubscribers\":" + std::to_string(recipients.recipientCount) +
					",\"activeRuns\":" + std::to_string(m_chatRunsById.size()) + "}");
			});
	}

} // namespace blazeclaw::gateway
