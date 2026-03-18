#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

    void GatewayHost::RegisterSecurityOpsHandlers() {
        m_dispatcher.Register("gateway.nodes.voice.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"wakeWord\":true,\"pushToTalk\":true,\"handsFree\":false,\"languages\":[\"en-US\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"still\":true,\"video\":false,\"maxWidth\":1920,\"maxHeight\":1080}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.channels", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channels\":[\"desktop\"],\"locationAware\":false,\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.access.entries", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":[],\"count\":0,\"mode\":\"allowlist\",\"source\":\"runtime\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.ops.doctor.run.preview", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"runId\":\"doctor-preview-1\",\"checks\":[\"transport\",\"session\",\"routing\"],\"count\":3,\"preview\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":false,\"wakeWord\":\"blaze\",\"talkMode\":\"push_to_talk\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.devices", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"devices\":[\"default-mic\"],\"activeDevice\":\"default-mic\",\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.permissions", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"microphone\":false,\"hotword\":false,\"granted\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.routing", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":\"local\",\"fallback\":\"push_to_talk\",\"priority\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"mode\":\"push_to_talk\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"voice.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"voice.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.voice.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"voice.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"available\":false,\"captureMode\":\"still\",\"lastCaptureMs\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.devices", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"devices\":[\"default-camera\"],\"activeDevice\":\"default-camera\",\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.permissions", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"camera\":false,\"capture\":false,\"granted\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.routing", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":\"local\",\"fallback\":\"still\",\"priority\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"mode\":\"still\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"camera.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"camera.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.camera.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"camera.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":false,\"locationHooked\":false,\"providers\":[\"desktop\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.providers", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"providers\":[\"desktop\"],\"defaultProvider\":\"desktop\",\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.permissions", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"notifications\":false,\"location\":false,\"granted\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.routing", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":\"desktop\",\"fallback\":\"none\",\"priority\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"mode\":\"desktop\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"notifications.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"notifications.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.notifications.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"notifications.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.access.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"mode\":\"allowlist\",\"enabled\":false,\"entries\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.dmPairing.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":false,\"policy\":\"manual\",\"pending\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.dmPairing.entries", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":[],\"count\":0,\"policy\":\"manual\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.allowlists.entries", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":[],\"count\":0,\"source\":\"runtime\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.allowlists.count", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"count\":0,\"mode\":\"allowlist\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":true,\"level\":\"info\",\"diagnostics\":\"seeded\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.levels", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"levels\":[\"debug\",\"info\",\"warn\",\"error\"],\"count\":4}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.targets", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"targets\":[\"memory\"],\"defaultTarget\":\"memory\",\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.retention", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":true,\"days\":7,\"maxEntries\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.filters", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"filters\":[\"level>=info\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.format", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"format\":\"json\",\"timestamp\":\"iso8601\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.pipeline", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pipeline\":\"seeded\",\"stages\":3,\"enabled\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.schemaVersion", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"schemaVersion\":\"1.0\",\"compatible\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.snapshot", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"snapshotId\":\"log-snapshot-1\",\"entries\":0,\"captured\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.window", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowMs\":60000,\"entries\":0,\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.sample", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"sample\":[],\"count\":0,\"source\":\"memory\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.recent", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"recent\":[],\"count\":0,\"windowMs\":60000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"throughput\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.catalog", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"catalog\":[\"status\",\"levels\",\"targets\"],\"count\":3}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"level\":\"info\",\"enabled\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"logging.window.default\",\"windowMs\":60000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.cursorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cursorKey\":\"logging.cursor.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.anchorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"anchorKey\":\"logging.anchor.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.offsetKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"offsetKey\":\"logging.offset.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.markerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"markerKey\":\"logging.marker.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.pointerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pointerKey\":\"logging.pointer.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"logging.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.sequenceKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"sequenceKey\":\"logging.sequence.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.streamKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"streamKey\":\"logging.stream.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.bundleKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"bundleKey\":\"logging.bundle.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.packageKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"packageKey\":\"logging.package.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.archiveKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"archiveKey\":\"logging.archive.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"logging.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.contextKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"contextKey\":\"logging.context.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.channelKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channelKey\":\"logging.channel.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.routeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"routeKey\":\"logging.route.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.accountKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"accountKey\":\"logging.account.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.logging.agentKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"agentKey\":\"logging.agent.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":true,\"sinks\":[\"memory\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.sinks", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"sinks\":[\"memory\"],\"defaultSink\":\"memory\",\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.events", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"events\":[\"gateway.health\",\"gateway.shutdown\"],\"count\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.export", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"format\":\"json\",\"supported\":true,\"destinations\":[\"file\"]}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.retention", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":true,\"days\":3,\"maxEvents\":500}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.channels", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channels\":[\"memory\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.pipeline", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pipeline\":\"seeded\",\"stages\":2,\"enabled\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.schemaVersion", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"schemaVersion\":\"1.0\",\"compatible\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.snapshotExport", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"exportId\":\"diag-snapshot-1\",\"format\":\"json\",\"supported\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowExport", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"exportId\":\"diag-window-1\",\"windowMs\":60000,\"supported\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.sample", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"sample\":[],\"count\":0,\"source\":\"memory\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.recent", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"recent\":[],\"count\":0,\"windowMs\":60000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"throughput\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.catalog", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"catalog\":[\"status\",\"sinks\",\"events\"],\"count\":3}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"sink\":\"memory\",\"enabled\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"diagnostics.window.default\",\"windowMs\":60000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.cursorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"cursorKey\":\"diagnostics.cursor.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.anchorKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"anchorKey\":\"diagnostics.anchor.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.offsetKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"offsetKey\":\"diagnostics.offset.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.markerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"markerKey\":\"diagnostics.marker.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.pointerKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pointerKey\":\"diagnostics.pointer.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"diagnostics.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.sequenceKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"sequenceKey\":\"diagnostics.sequence.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.streamKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"streamKey\":\"diagnostics.stream.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.bundleKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"bundleKey\":\"diagnostics.bundle.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.packageKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"packageKey\":\"diagnostics.package.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.archiveKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"archiveKey\":\"diagnostics.archive.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"diagnostics.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.contextKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"contextKey\":\"diagnostics.context.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.channelKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"channelKey\":\"diagnostics.channel.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.routeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"routeKey\":\"diagnostics.route.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.accountKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"accountKey\":\"diagnostics.account.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.security.diagnostics.agentKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"agentKey\":\"diagnostics.agent.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.ops.doctor.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"checks\":3,\"doctorAvailable\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.ops.doctor.run.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"running\":false,\"lastRunId\":\"doctor-preview-1\",\"lastStatus\":\"ok\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"host\":\"a2ui\",\"available\":false,\"session\":\"none\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"host\":\"a2ui\",\"layers\":true,\"annotations\":true,\"maxSurfaces\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.session", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"session\":\"none\",\"attached\":false,\"surfaces\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.permissions", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"draw\":false,\"annotate\":false,\"granted\":false}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.routing", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"route\":\"local\",\"fallback\":\"none\",\"priority\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"host\":\"a2ui\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"canvas.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"canvas.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.nodes.canvas.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"canvas.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"desktopActions\":true,\"commandSurface\":\"seeded\",\"coverage\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.commands", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"commands\":[\"gateway.ping\",\"gateway.health\"],\"count\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.shortcuts", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"shortcuts\":[\"Ctrl+L\",\"Ctrl+R\"],\"count\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.aliases", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"aliases\":[\"ping\",\"health\"],\"count\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"interactive\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.context", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"workspace\":\"default\",\"scope\":\"desktop\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"throughput\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.catalog", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"catalog\":[\"commands\",\"shortcuts\",\"aliases\"],\"count\":3}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"cli.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"cli.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.cli.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"cli.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.status", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"hosting\":false,\"endpoint\":\"\",\"surface\":\"control\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.endpoint", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":false,\"url\":\"\",\"surface\":\"control\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.routes", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"routes\":[\"/\",\"/health\"],\"count\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.health", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.originPolicy", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"mode\":\"local-only\",\"allowed\":[\"http://localhost\"],\"count\":1}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.csrf", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":true,\"mode\":\"token\",\"sameSite\":\"strict\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.latency", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.profile", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"profile\":\"default\",\"hosting\":false,\"surface\":\"control\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.metrics", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"samples\":1,\"errorRate\":0,\"throughput\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.catalog", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"catalog\":[\"routes\",\"health\",\"profile\"],\"count\":3}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowKey\":\"web.window.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"tokenKey\":\"web.token.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.platform.web.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"scopeKey\":\"web.scope.default\",\"active\":true}",
                .error = std::nullopt,
            };
            });
    }

} // namespace blazeclaw::gateway
