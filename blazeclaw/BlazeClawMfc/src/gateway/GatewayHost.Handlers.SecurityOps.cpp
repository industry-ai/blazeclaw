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
    }

} // namespace blazeclaw::gateway
