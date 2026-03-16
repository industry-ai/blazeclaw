#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {
    namespace {
        protocol::ResponseFrame BuildStaticResponse(
            const protocol::RequestFrame& request,
            const std::string& payloadJson) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = payloadJson,
                .error = std::nullopt,
            };
        }
    }

    void GatewayHost::RegisterEventHandlers() {
        m_dispatcher.Register("gateway.events.batch", [](const protocol::RequestFrame& request) {
         return BuildStaticResponse(request, "{\"batches\":[\"lifecycle\",\"updates\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.events.cursor", [](const protocol::RequestFrame& request) {
         return BuildStaticResponse(request, "{\"cursor\":\"evt-2\",\"event\":\"gateway.session.reset\"}");
            });

        m_dispatcher.Register("gateway.events.anchor", [](const protocol::RequestFrame& request) {
         return BuildStaticResponse(request, "{\"anchor\":\"evt-1\",\"event\":\"gateway.shutdown\"}");
            });

        m_dispatcher.Register("gateway.events.offset", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"offset\":1,\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.marker", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"marker\":\"evt-marker-1\",\"event\":\"gateway.shutdown\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.sequence", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"sequence\":1,\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.pointer", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"pointer\":\"evt-pointer-1\",\"event\":\"gateway.shutdown\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.token", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"token\":\"evt-token-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.stream", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"stream\":\"evt-stream-1\",\"event\":\"gateway.tools.catalog.update\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.windowId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"windowId\":\"evt-window-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.sessionKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"sessionKey\":\"sess-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"scopeKey\":\"scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.contextKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"contextKey\":\"context-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.channelKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"channelKey\":\"channel-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.routeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"routeKey\":\"route-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.accountKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"accountKey\":\"account-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.agentKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"agentKey\":\"agent-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.modelKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"modelKey\":\"model-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.configKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"configKey\":\"config-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.policyKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"policyKey\":\"policy-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.toolKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"toolKey\":\"tool-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.transportKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"transportKey\":\"transport-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.runtimeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"runtimeKey\":\"runtime-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"stateKey\":\"state-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.healthKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"healthKey\":\"health-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.logKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"logKey\":\"log-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.metricKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"metricKey\":\"metric-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.traceKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"traceKey\":\"trace-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.auditKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"auditKey\":\"audit-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.debugKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"debugKey\":\"debug-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.cacheKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"cacheKey\":\"cache-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.queueKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"queueKey\":\"queue-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"windowKey\":\"window-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.cursorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"cursorKey\":\"cursor-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.anchorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"anchorKey\":\"anchor-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.offsetKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"offsetKey\":\"offset-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.markerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"markerKey\":\"marker-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.pointerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"pointerKey\":\"pointer-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"tokenKey\":\"token-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.sequenceKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"sequenceKey\":\"sequence-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.streamKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"streamKey\":\"stream-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.bundleKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"bundleKey\":\"bundle-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.packageKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"packageKey\":\"package-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.archiveKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"archiveKey\":\"archive-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.manifestKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"manifestKey\":\"manifest-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.profileKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"profileKey\":\"profile-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.templateKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"templateKey\":\"template-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.revisionKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"revisionKey\":\"revision-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.historyKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"historyKey\":\"history-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.snapshotKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"snapshotKey\":\"snapshot-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.indexKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"indexKey\":\"index-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"windowScopeKey\":\"window-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"cursorScopeKey\":\"cursor-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.anchorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"anchorScopeKey\":\"anchor-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.offsetScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"offsetScopeKey\":\"offset-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.pointerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"pointerScopeKey\":\"pointer-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"tokenScopeKey\":\"token-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.streamScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"streamScopeKey\":\"stream-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.sequenceScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"sequenceScopeKey\":\"sequence-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.bundleScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"bundleScopeKey\":\"bundle-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.packageScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"packageScopeKey\":\"package-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.archiveScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"archiveScopeKey\":\"archive-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.manifestScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"manifestScopeKey\":\"manifest-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.profileScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"profileScopeKey\":\"profile-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.templateScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"templateScopeKey\":\"template-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.revisionScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"revisionScopeKey\":\"revision-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.historyScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"historyScopeKey\":\"history-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.snapshotScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"snapshotScopeKey\":\"snapshot-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.indexScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"indexScopeKey\":\"index-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"windowScopeId\":\"window-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"cursorScopeId\":\"cursor-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.anchorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"anchorScopeId\":\"anchor-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.offsetScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"offsetScopeId\":\"offset-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.pointerScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"pointerScopeId\":\"pointer-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.tokenScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"tokenScopeId\":\"token-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.sequenceScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"sequenceScopeId\":\"sequence-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.streamScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"streamScopeId\":\"stream-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.bundleScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"bundleScopeId\":\"bundle-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.packageScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"packageScopeId\":\"package-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.archiveScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"archiveScopeId\":\"archive-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.manifestScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"manifestScopeId\":\"manifest-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.profileScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"profileScopeId\":\"profile-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.templateScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"templateScopeId\":\"template-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.revisionScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"revisionScopeId\":\"revision-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.historyScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"historyScopeId\":\"history-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.snapshotScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"snapshotScopeId\":\"snapshot-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.indexScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"indexScopeId\":\"index-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.markerScopeId", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"markerScopeId\":\"marker-scope-id-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.markerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson = "{\"markerScopeKey\":\"marker-scope-key-1\",\"event\":\"gateway.session.reset\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.events.recent", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"events\":[\"gateway.shutdown\",\"gateway.session.reset\"],\"count\":2}",
                .error = std::nullopt,
            };
            });
    }

} // namespace blazeclaw::gateway
