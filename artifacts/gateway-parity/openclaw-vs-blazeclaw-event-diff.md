# OpenClaw vs BlazeClaw Gateway Event Diff

Generated: 2026-04-23T08:18:54.149Z

## Inputs

- OpenClaw event list source: `openclaw/src/gateway/server-methods-list.ts`
- BlazeClaw scanned files: **10**
- Extraction: OpenClaw GATEWAY_EVENTS array literals; BlazeClaw literal event-string evidence scan.

## Summary

- OpenClaw event count: **23**
- Implemented evidence count: **13**
- Deferred/unmatched count: **10**
- Literal evidence coverage: **56.52%**

## Event Mapping

- `agent` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `chat` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`
- `connect.challenge` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayWebSocketTransport.cpp`
- `cron` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
- `device.pair.requested` — **deferred**
  - Evidence: _none in scanned files_
- `device.pair.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `exec.approval.requested` — **deferred**
  - Evidence: _none in scanned files_
- `exec.approval.resolved` — **deferred**
  - Evidence: _none in scanned files_
- `health` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `heartbeat` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`
- `node.invoke.request` — **deferred**
  - Evidence: _none in scanned files_
- `node.pair.requested` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `node.pair.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `plugin.approval.requested` — **deferred**
  - Evidence: _none in scanned files_
- `plugin.approval.resolved` — **deferred**
  - Evidence: _none in scanned files_
- `presence` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`
- `session.message` — **deferred**
  - Evidence: _none in scanned files_
- `session.tool` — **deferred**
  - Evidence: _none in scanned files_
- `sessions.changed` — **deferred**
  - Evidence: _none in scanned files_
- `shutdown` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `talk.mode` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
- `tick` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`
- `voicewake.changed` — **deferred**
  - Evidence: _none in scanned files_
