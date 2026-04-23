# OpenClaw vs BlazeClaw Gateway Event Diff

Generated: 2026-04-23T08:58:28.321Z

## Inputs

- OpenClaw event list source: `openclaw/src/gateway/server-methods-list.ts`
- BlazeClaw scanned files: **12**
- Extraction: OpenClaw GATEWAY_EVENTS array literals; BlazeClaw literal event-string evidence scan.

## Summary

- OpenClaw event count: **24**
- Implemented evidence count: **24**
- Deferred/unmatched count: **0**
- Literal evidence coverage: **100%**

## Event Mapping

- `agent` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.ConfigDiagnostics.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.AgentSessionMutation.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `chat` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.AgentSessionMutation.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`
- `connect.challenge` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayWebSocketTransport.cpp`
- `cron` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.ConfigDiagnostics.cpp`
- `device.pair.requested` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `device.pair.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `exec.approval.requested` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `exec.approval.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `health` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.ConfigDiagnostics.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Events.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `heartbeat` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`
- `node.invoke.request` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `node.pair.requested` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `node.pair.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `plugin.approval.requested` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `plugin.approval.resolved` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`
- `presence` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Transport.cpp`
- `session.message` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`
- `session.tool` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.ChatPipeline.cpp`
- `sessions.changed` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.ConfigDiagnostics.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.AgentSessionMutation.cpp`
- `shutdown` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.SecurityOps.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/generated/GatewayHandlerCatalog.Generated.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHandlers.manifest.json`
- `talk.mode` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
- `tick` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.cpp`, `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.EventCatalogQuery.cpp`
- `update.available` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.ConfigDiagnostics.cpp`
- `voicewake.changed` — **implemented**
  - Evidence: `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.Runtime.Surface.cpp`
