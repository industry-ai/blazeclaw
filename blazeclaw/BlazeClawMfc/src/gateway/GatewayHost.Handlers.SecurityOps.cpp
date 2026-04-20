#include "pch.h"
#include "GatewayHost.h"

namespace blazeclaw::gateway {

namespace {

void RegisterMethodAlias(
	GatewayMethodDispatcher& dispatcher,
	const std::string& aliasMethod,
	const std::string& targetMethod)
{
	dispatcher.Register(
		aliasMethod,
		[&dispatcher, targetMethod](const protocol::RequestFrame& request) {
			auto forwarded = request;
			forwarded.method = targetMethod;
			return dispatcher.Dispatch(forwarded);
		});
}

void RegisterStaticMethod(
	GatewayMethodDispatcher& dispatcher,
	const std::string& method,
	const std::string& payloadJson)
{
	dispatcher.Register(
		method,
		[payloadJson](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, payloadJson);
		});
}

} // namespace

    void GatewayHost::RegisterSecurityOpsHandlers() {
        m_dispatcher.Register("gateway.nodes.voice.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"wakeWord\":true,\"pushToTalk\":true,\"handsFree\":false,\"languages\":[\"en-US\"],\"count\":1}");
            });

        m_dispatcher.Register(
            "gateway.nodes.voice.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"voice.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.voice.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"voice.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.camera.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"camera.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.voice.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"voice.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.notifications.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"notifications.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.camera.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"camera.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.templateScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"templateScopeId2\":\"logging.templateScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.revisionScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"revisionScopeId2\":\"logging.revisionScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.historyScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"historyScopeId2\":\"logging.historyScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.snapshotScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"snapshotScopeId2\":\"logging.snapshotScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.indexScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"indexScopeId2\":\"logging.indexScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.windowScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"windowScopeId2\":\"logging.windowScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"still\":true,\"video\":false,\"maxWidth\":1920,\"maxHeight\":1080}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.templateScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"templateScopeId2\":\"diagnostics.templateScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.revisionScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"revisionScopeId2\":\"diagnostics.revisionScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.historyScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"historyScopeId2\":\"diagnostics.historyScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.snapshotScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"snapshotScopeId2\":\"diagnostics.snapshotScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.indexScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"indexScopeId2\":\"diagnostics.indexScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.windowScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"windowScopeId2\":\"diagnostics.windowScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.notifications.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"notifications.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.canvas.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"canvas.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.camera.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"camera.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.cli.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"cli.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.sequenceScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId3\":\"logging.sequenceScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.web.streamScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId\":\"web.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.streamScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId3\":\"logging.streamScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.bundleScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"bundleScopeId3\":\"logging.bundleScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.packageScopeId4",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId4\":\"logging.packageScopeId4.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.archiveScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId3\":\"logging.archiveScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.manifestScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"manifestScopeId2\":\"logging.manifestScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.channels", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"channels\":[\"desktop\"],\"locationAware\":false,\"count\":1}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.sequenceScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId3\":\"diagnostics.sequenceScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.streamScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId3\":\"diagnostics.streamScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.bundleScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"bundleScopeId3\":\"diagnostics.bundleScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.packageScopeId4",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId4\":\"diagnostics.packageScopeId4.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.archiveScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId3\":\"diagnostics.archiveScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.manifestScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"manifestScopeId2\":\"diagnostics.manifestScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.notifications.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"notifications.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.canvas.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"canvas.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.access.entries", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"mode\":\"allowlist\",\"source\":\"runtime\"}");
            });

        m_dispatcher.Register(
            "gateway.platform.cli.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"cli.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.pointerScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId2\":\"logging.pointerScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.web.sequenceScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId\":\"web.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.tokenScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId2\":\"logging.tokenScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.sequenceScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId2\":\"logging.sequenceScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.streamScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId2\":\"logging.streamScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.bundleScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"bundleScopeId2\":\"logging.bundleScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.packageScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId3\":\"logging.packageScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.ops.doctor.run.preview", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"runId\":\"doctor-preview-1\",\"checks\":[\"transport\",\"session\",\"routing\"],\"count\":3,\"preview\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.pointerScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId2\":\"diagnostics.pointerScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.tokenScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId2\":\"diagnostics.tokenScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.sequenceScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"sequenceScopeId2\":\"diagnostics.sequenceScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.streamScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"streamScopeId2\":\"diagnostics.streamScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.bundleScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"bundleScopeId2\":\"diagnostics.bundleScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.packageScopeId3",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId3\":\"diagnostics.packageScopeId3.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":false,\"wakeWord\":\"blaze\",\"talkMode\":\"push_to_talk\"}");
            });

        m_dispatcher.Register(
            "gateway.nodes.canvas.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"canvas.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.devices", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"devices\":[\"default-mic\"],\"activeDevice\":\"default-mic\",\"count\":1}");
            });

        m_dispatcher.Register(
            "gateway.platform.cli.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"cli.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.permissions", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"microphone\":false,\"hotword\":false,\"granted\":false}");
            });

        m_dispatcher.Register(
            "gateway.platform.web.pointerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"pointerScopeId\":\"web.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.routing", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"push_to_talk\",\"priority\":1}");
            });

        m_dispatcher.Register("gateway.nodes.voice.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.nodes.voice.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
            });

        m_dispatcher.Register("gateway.nodes.voice.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
            });

        m_dispatcher.Register("gateway.nodes.voice.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"push_to_talk\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"voice.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"voice.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"voice.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"voice.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"voice.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"voice.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"voice.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"voice.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.voice.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"voice.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.voice.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"voice.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.voice.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"voice.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"available\":false,\"captureMode\":\"still\",\"lastCaptureMs\":0}");
            });

        m_dispatcher.Register("gateway.nodes.camera.devices", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"devices\":[\"default-camera\"],\"activeDevice\":\"default-camera\",\"count\":1}");
            });

        m_dispatcher.Register("gateway.nodes.camera.permissions", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"camera\":false,\"capture\":false,\"granted\":false}");
            });

        m_dispatcher.Register("gateway.nodes.camera.routing", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"still\",\"priority\":1}");
            });

        m_dispatcher.Register("gateway.nodes.camera.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.nodes.camera.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
            });

        m_dispatcher.Register("gateway.nodes.camera.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
            });

        m_dispatcher.Register("gateway.nodes.camera.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"still\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"camera.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"camera.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"camera.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"camera.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"camera.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"camera.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"camera.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"camera.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.camera.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"camera.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.camera.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"camera.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.camera.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"camera.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":false,\"locationHooked\":false,\"providers\":[\"desktop\"],\"count\":1}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.providers", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"providers\":[\"desktop\"],\"defaultProvider\":\"desktop\",\"count\":1}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.permissions", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"notifications\":false,\"location\":false,\"granted\":false}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.routing", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"route\":\"desktop\",\"fallback\":\"none\",\"priority\":1}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"desktop\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"notifications.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"notifications.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"notifications.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"notifications.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"notifications.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"notifications.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"notifications.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"notifications.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.notifications.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"notifications.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.notifications.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"notifications.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.notifications.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"notifications.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.historyScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"historyScopeId\":\"logging.historyScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.snapshotScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"snapshotScopeId\":\"logging.snapshotScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.indexScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"indexScopeId\":\"logging.indexScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.markerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"markerScopeId\":\"logging.markerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.packageScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId2\":\"logging.packageScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.archiveScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId2\":\"logging.archiveScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.access.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"mode\":\"allowlist\",\"enabled\":false,\"entries\":0}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.historyScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"historyScopeId\":\"diagnostics.historyScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.snapshotScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"snapshotScopeId\":\"diagnostics.snapshotScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.indexScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"indexScopeId\":\"diagnostics.indexScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.markerScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"markerScopeId\":\"diagnostics.markerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.packageScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId2\":\"diagnostics.packageScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.archiveScopeId2",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId2\":\"diagnostics.archiveScopeId2.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.dmPairing.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":false,\"policy\":\"manual\",\"pending\":0}");
            });

        m_dispatcher.Register("gateway.security.dmPairing.entries", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"policy\":\"manual\"}");
            });

        m_dispatcher.Register("gateway.security.allowlists.entries", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"source\":\"runtime\"}");
            });

        m_dispatcher.Register("gateway.security.allowlists.count", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"count\":0,\"mode\":\"allowlist\"}");
            });

        m_dispatcher.Register("gateway.security.logging.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":true,\"level\":\"info\",\"diagnostics\":\"seeded\"}");
            });

        m_dispatcher.Register("gateway.security.logging.levels", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"levels\":[\"debug\",\"info\",\"warn\",\"error\"],\"count\":4}");
            });

        m_dispatcher.Register("gateway.security.logging.targets", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"targets\":[\"memory\"],\"defaultTarget\":\"memory\",\"count\":1}");
            });

        m_dispatcher.Register("gateway.security.logging.retention", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":true,\"days\":7,\"maxEntries\":1000}");
            });

        m_dispatcher.Register("gateway.security.logging.filters", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"filters\":[\"level>=info\"],\"count\":1}");
            });

        m_dispatcher.Register("gateway.security.logging.format", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"format\":\"json\",\"timestamp\":\"iso8601\"}");
            });

        m_dispatcher.Register("gateway.security.logging.pipeline", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pipeline\":\"seeded\",\"stages\":3,\"enabled\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.schemaVersion", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"schemaVersion\":\"1.0\",\"compatible\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.snapshot", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"snapshotId\":\"log-snapshot-1\",\"entries\":0,\"captured\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.window", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowMs\":60000,\"entries\":0,\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.sample", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sample\":[],\"count\":0,\"source\":\"memory\"}");
            });

        m_dispatcher.Register("gateway.security.logging.recent", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"recent\":[],\"count\":0,\"windowMs\":60000}");
            });

        m_dispatcher.Register("gateway.security.logging.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
            });

        m_dispatcher.Register("gateway.security.logging.catalog", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"catalog\":[\"status\",\"levels\",\"targets\"],\"count\":3}");
            });

        m_dispatcher.Register("gateway.security.logging.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"level\":\"info\",\"enabled\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"logging.window.default\",\"windowMs\":60000}");
            });

        m_dispatcher.Register("gateway.security.logging.cursorKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorKey\":\"logging.cursor.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.anchorKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorKey\":\"logging.anchor.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.offsetKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetKey\":\"logging.offset.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.markerKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"markerKey\":\"logging.marker.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.pointerKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerKey\":\"logging.pointer.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"logging.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.sequenceKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceKey\":\"logging.sequence.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.streamKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamKey\":\"logging.stream.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.bundleKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleKey\":\"logging.bundle.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.packageKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"packageKey\":\"logging.package.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.archiveKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"archiveKey\":\"logging.archive.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"logging.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.contextKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"contextKey\":\"logging.context.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.channelKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"channelKey\":\"logging.channel.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.routeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"routeKey\":\"logging.route.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.accountKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"accountKey\":\"logging.account.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.agentKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"agentKey\":\"logging.agent.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"logging.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.healthKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthKey\":\"logging.health.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.logKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"logKey\":\"logging.log.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.metricKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"metricKey\":\"logging.metric.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.traceKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"traceKey\":\"logging.trace.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.debugKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"debugKey\":\"logging.debug.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.cacheKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cacheKey\":\"logging.cache.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.queueKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"queueKey\":\"logging.queue.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"logging.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"logging.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.anchorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorScopeKey\":\"logging.anchorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.offsetScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetScopeKey\":\"logging.offsetScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.markerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"markerScopeKey\":\"logging.markerScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.pointerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerScopeKey\":\"logging.pointerScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"logging.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.streamScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamScopeKey\":\"logging.streamScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.sequenceScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceScopeKey\":\"logging.sequenceScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.bundleScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleScopeKey\":\"logging.bundleScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.packageScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"packageScopeKey\":\"logging.packageScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.archiveScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"archiveScopeKey\":\"logging.archiveScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.manifestScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"manifestScopeKey\":\"logging.manifestScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.profileScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profileScopeKey\":\"logging.profileScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.templateScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"templateScopeKey\":\"logging.templateScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.revisionScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"revisionScopeKey\":\"logging.revisionScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.historyScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"historyScopeKey\":\"logging.historyScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.snapshotScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"snapshotScopeKey\":\"logging.snapshotScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.indexScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"indexScopeKey\":\"logging.indexScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"logging.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"logging.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.anchorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorScopeId\":\"logging.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.offsetScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetScopeId\":\"logging.offsetScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.pointerScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerScopeId\":\"logging.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.tokenScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeId\":\"logging.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.sequenceScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceScopeId\":\"logging.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.streamScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamScopeId\":\"logging.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.logging.bundleScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleScopeId\":\"logging.bundleScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.packageScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId\":\"logging.packageScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.archiveScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId\":\"logging.archiveScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.manifestScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"manifestScopeId\":\"logging.manifestScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.profileScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"profileScopeId\":\"logging.profileScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.templateScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"templateScopeId\":\"logging.templateScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.logging.revisionScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"revisionScopeId\":\"logging.revisionScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":true,\"sinks\":[\"memory\"],\"count\":1}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.sinks", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sinks\":[\"memory\"],\"defaultSink\":\"memory\",\"count\":1}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.events", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"events\":[\"gateway.health\",\"gateway.shutdown\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.export", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"format\":\"json\",\"supported\":true,\"destinations\":[\"file\"]}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.retention", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":true,\"days\":3,\"maxEvents\":500}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.channels", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"channels\":[\"memory\"],\"count\":1}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.pipeline", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pipeline\":\"seeded\",\"stages\":2,\"enabled\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.schemaVersion", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"schemaVersion\":\"1.0\",\"compatible\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.snapshotExport", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"exportId\":\"diag-snapshot-1\",\"format\":\"json\",\"supported\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowExport", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"exportId\":\"diag-window-1\",\"windowMs\":60000,\"supported\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.sample", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sample\":[],\"count\":0,\"source\":\"memory\"}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.recent", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"recent\":[],\"count\":0,\"windowMs\":60000}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.catalog", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"catalog\":[\"status\",\"sinks\",\"events\"],\"count\":3}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"sink\":\"memory\",\"enabled\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"diagnostics.window.default\",\"windowMs\":60000}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.cursorKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorKey\":\"diagnostics.cursor.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.anchorKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorKey\":\"diagnostics.anchor.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.offsetKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetKey\":\"diagnostics.offset.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.markerKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"markerKey\":\"diagnostics.marker.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.pointerKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerKey\":\"diagnostics.pointer.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"diagnostics.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.sequenceKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceKey\":\"diagnostics.sequence.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.streamKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamKey\":\"diagnostics.stream.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.bundleKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleKey\":\"diagnostics.bundle.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.packageKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"packageKey\":\"diagnostics.package.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.archiveKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"archiveKey\":\"diagnostics.archive.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"diagnostics.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.contextKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"contextKey\":\"diagnostics.context.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.channelKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"channelKey\":\"diagnostics.channel.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.routeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"routeKey\":\"diagnostics.route.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.accountKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"accountKey\":\"diagnostics.account.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.agentKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"agentKey\":\"diagnostics.agent.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"diagnostics.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.healthKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthKey\":\"diagnostics.health.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.logKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"logKey\":\"diagnostics.log.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.metricKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"metricKey\":\"diagnostics.metric.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.traceKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"traceKey\":\"diagnostics.trace.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.debugKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"debugKey\":\"diagnostics.debug.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.cacheKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cacheKey\":\"diagnostics.cache.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.queueKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"queueKey\":\"diagnostics.queue.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"diagnostics.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"diagnostics.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.anchorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorScopeKey\":\"diagnostics.anchorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.offsetScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetScopeKey\":\"diagnostics.offsetScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.markerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"markerScopeKey\":\"diagnostics.markerScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.pointerScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerScopeKey\":\"diagnostics.pointerScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"diagnostics.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.streamScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamScopeKey\":\"diagnostics.streamScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.sequenceScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceScopeKey\":\"diagnostics.sequenceScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.bundleScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleScopeKey\":\"diagnostics.bundleScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.packageScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"packageScopeKey\":\"diagnostics.packageScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.archiveScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"archiveScopeKey\":\"diagnostics.archiveScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.manifestScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"manifestScopeKey\":\"diagnostics.manifestScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.profileScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profileScopeKey\":\"diagnostics.profileScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.templateScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"templateScopeKey\":\"diagnostics.templateScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.revisionScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"revisionScopeKey\":\"diagnostics.revisionScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.historyScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"historyScopeKey\":\"diagnostics.historyScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.snapshotScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"snapshotScopeKey\":\"diagnostics.snapshotScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.indexScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"indexScopeKey\":\"diagnostics.indexScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"diagnostics.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"diagnostics.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.anchorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"anchorScopeId\":\"diagnostics.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.offsetScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"offsetScopeId\":\"diagnostics.offsetScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.pointerScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"pointerScopeId\":\"diagnostics.pointerScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.tokenScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeId\":\"diagnostics.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.sequenceScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"sequenceScopeId\":\"diagnostics.sequenceScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.streamScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"streamScopeId\":\"diagnostics.streamScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.security.diagnostics.bundleScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"bundleScopeId\":\"diagnostics.bundleScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.packageScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"packageScopeId\":\"diagnostics.packageScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.archiveScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"archiveScopeId\":\"diagnostics.archiveScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.manifestScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"manifestScopeId\":\"diagnostics.manifestScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.profileScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"profileScopeId\":\"diagnostics.profileScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.templateScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"templateScopeId\":\"diagnostics.templateScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.security.diagnostics.revisionScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"revisionScopeId\":\"diagnostics.revisionScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.ops.doctor.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"checks\":3,\"doctorAvailable\":true}");
            });

        m_dispatcher.Register("gateway.ops.doctor.run.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"running\":false,\"lastRunId\":\"doctor-preview-1\",\"lastStatus\":\"ok\"}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"host\":\"a2ui\",\"available\":false,\"session\":\"none\"}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.capabilities", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"host\":\"a2ui\",\"layers\":true,\"annotations\":true,\"maxSurfaces\":1}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.session", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"session\":\"none\",\"attached\":false,\"surfaces\":0}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.permissions", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"draw\":false,\"annotate\":false,\"granted\":false}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.routing", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"none\",\"priority\":1}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"host\":\"a2ui\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"canvas.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"canvas.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"canvas.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"canvas.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"canvas.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"canvas.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"canvas.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"canvas.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.nodes.canvas.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"canvas.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.canvas.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"canvas.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.nodes.canvas.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"canvas.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"desktopActions\":true,\"commandSurface\":\"seeded\",\"coverage\":0}");
            });

        m_dispatcher.Register("gateway.platform.cli.commands", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"commands\":[\"gateway.ping\",\"gateway.health\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.platform.cli.shortcuts", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"shortcuts\":[\"Ctrl+L\",\"Ctrl+R\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.platform.cli.aliases", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"aliases\":[\"ping\",\"health\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.platform.cli.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"interactive\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.context", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"workspace\":\"default\",\"scope\":\"desktop\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.platform.cli.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}");
            });

        m_dispatcher.Register("gateway.platform.cli.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
            });

        m_dispatcher.Register("gateway.platform.cli.catalog", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"catalog\":[\"commands\",\"shortcuts\",\"aliases\"],\"count\":3}");
            });

        m_dispatcher.Register("gateway.platform.cli.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"cli.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"cli.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"cli.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"cli.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"cli.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"cli.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"cli.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"cli.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.cli.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"cli.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.cli.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"cli.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.cli.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"cli.tokenScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.status", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"hosting\":false,\"endpoint\":\"\",\"surface\":\"control\"}");
            });

        m_dispatcher.Register("gateway.platform.web.endpoint", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":false,\"url\":\"\",\"surface\":\"control\"}");
            });

        m_dispatcher.Register("gateway.platform.web.routes", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"routes\":[\"/\",\"/health\"],\"count\":2}");
            });

        m_dispatcher.Register("gateway.platform.web.health", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}");
            });

        m_dispatcher.Register("gateway.platform.web.originPolicy", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"mode\":\"local-only\",\"allowed\":[\"http://localhost\"],\"count\":1}");
            });

        m_dispatcher.Register("gateway.platform.web.csrf", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"enabled\":true,\"mode\":\"token\",\"sameSite\":\"strict\"}");
            });

        m_dispatcher.Register("gateway.platform.web.latency", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
            });

        m_dispatcher.Register("gateway.platform.web.profile", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"profile\":\"default\",\"hosting\":false,\"surface\":\"control\"}");
            });

        m_dispatcher.Register("gateway.platform.web.metrics", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
            });

        m_dispatcher.Register("gateway.platform.web.catalog", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"catalog\":[\"routes\",\"health\",\"profile\"],\"count\":3}");
            });

        m_dispatcher.Register("gateway.platform.web.windowKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowKey\":\"web.window.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.tokenKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenKey\":\"web.token.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.scopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"scopeKey\":\"web.scope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.stateKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"stateKey\":\"web.state.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.windowScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeKey\":\"web.windowScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.cursorScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeKey\":\"web.cursorScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.tokenScopeKey", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"tokenScopeKey\":\"web.tokenScope.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.windowScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"windowScopeId\":\"web.windowScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register("gateway.platform.web.cursorScopeId", [](const protocol::RequestFrame& request) {
            return protocol::OkResponse(request, "{\"cursorScopeId\":\"web.cursorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.web.anchorScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"anchorScopeId\":\"web.anchorScopeId.default\",\"active\":true}");
            });

        m_dispatcher.Register(
            "gateway.platform.web.tokenScopeId",
            [](const protocol::RequestFrame& request) {
                return protocol::OkResponse(request, "{\"tokenScopeId\":\"web.tokenScopeId.default\",\"active\":true}");
            });

		// OpenClaw P0 contract-parity aliases and stubs (Phase G / P0).
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approvals.get",
			"{\"scope\":\"global\",\"defaultMode\":\"manual\",\"updated\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approvals.set",
			"{\"scope\":\"global\",\"defaultMode\":\"manual\",\"updated\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approvals.node.get",
			"{\"scope\":\"node\",\"defaultMode\":\"manual\",\"updated\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approvals.node.set",
			"{\"scope\":\"node\",\"defaultMode\":\"manual\",\"updated\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approval.get",
			"{\"requestId\":\"exec-approval-1\",\"status\":\"pending\",\"found\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approval.list",
			"{\"items\":[],\"count\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approval.request",
			"{\"requestId\":\"exec-approval-1\",\"status\":\"pending\",\"queued\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approval.waitDecision",
			"{\"requestId\":\"exec-approval-1\",\"status\":\"pending\",\"resolved\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"exec.approval.resolve",
			"{\"requestId\":\"exec-approval-1\",\"status\":\"resolved\",\"resolved\":true}");

		RegisterStaticMethod(
			m_dispatcher,
			"plugin.approval.list",
			"{\"items\":[],\"count\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"plugin.approval.request",
			"{\"requestId\":\"plugin-approval-1\",\"status\":\"pending\",\"queued\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"plugin.approval.waitDecision",
			"{\"requestId\":\"plugin-approval-1\",\"status\":\"pending\",\"resolved\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"plugin.approval.resolve",
			"{\"requestId\":\"plugin-approval-1\",\"status\":\"resolved\",\"resolved\":true}");

		RegisterStaticMethod(
			m_dispatcher,
			"node.pair.request",
			"{\"nodeId\":\"node-1\",\"status\":\"pending\",\"paired\":false}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pair.list",
			"{\"pairs\":[],\"count\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pair.approve",
			"{\"nodeId\":\"node-1\",\"approved\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pair.reject",
			"{\"nodeId\":\"node-1\",\"rejected\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pair.verify",
			"{\"nodeId\":\"node-1\",\"verified\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.rename",
			"{\"nodeId\":\"node-1\",\"renamed\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.list",
			"{\"nodes\":[{\"id\":\"voice-node\"},{\"id\":\"camera-node\"},{\"id\":\"canvas-node\"}],\"count\":3}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.describe",
			"{\"nodeId\":\"node-1\",\"status\":\"idle\",\"online\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pending.drain",
			"{\"drained\":0,\"remaining\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pending.enqueue",
			"{\"accepted\":true,\"queueDepth\":1}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pending.pull",
			"{\"items\":[],\"count\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.pending.ack",
			"{\"acked\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.invoke",
			"{\"runId\":\"node-run-1\",\"queued\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.invoke.result",
			"{\"runId\":\"node-run-1\",\"accepted\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"node.event",
			"{\"accepted\":true,\"eventId\":\"node-event-1\"}");
		RegisterMethodAlias(
			m_dispatcher,
			"node.canvas.capability.refresh",
			"gateway.nodes.canvas.capabilities");

		RegisterStaticMethod(
			m_dispatcher,
			"device.pair.list",
			"{\"pairs\":[],\"count\":0}");
		RegisterStaticMethod(
			m_dispatcher,
			"device.pair.approve",
			"{\"deviceId\":\"device-1\",\"approved\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"device.pair.reject",
			"{\"deviceId\":\"device-1\",\"rejected\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"device.pair.remove",
			"{\"deviceId\":\"device-1\",\"removed\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"device.token.rotate",
			"{\"deviceId\":\"device-1\",\"rotated\":true}");
		RegisterStaticMethod(
			m_dispatcher,
			"device.token.revoke",
			"{\"deviceId\":\"device-1\",\"revoked\":true}");
    }

} // namespace blazeclaw::gateway
