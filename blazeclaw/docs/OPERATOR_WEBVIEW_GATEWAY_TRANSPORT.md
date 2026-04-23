# Operator guide: WebView chat and gateway transport modes

**Audience:** Developers and operators testing BlazeClaw’s embedded chat or comparing behavior to OpenClaw’s control UI.  
**Related:** [`docs/compare/ui.controllers.md`](../../docs/compare/ui.controllers.md), [`GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`](./GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md), [`UI_CONTROLLERS_GAP_SUGGESTIONS_IMPLEMENTATION_TRACKER.md`](../../docs/compare/UI_CONTROLLERS_GAP_SUGGESTIONS_IMPLEMENTATION_TRACKER.md).

---

## Two transport topologies

### A. Embedded in-process gateway (default desktop chat)

| Aspect | Detail |
|--------|--------|
| **What it is** | The WebView2 chat page runs inside MFC. RPC calls use `window.chrome.webview.postMessage` on channel **`blazeclaw.gateway.rpc`** (see `blazeclaw/BlazeClawMfc/web/chat/chat-controller.js`). |
| **Routing** | `CBlazeClawMFCView::HandleWebMessageJson` → `CBlazeClawMFCApp::RouteGatewayRequest` → in-process `GatewayHost` dispatcher. |
| **Auth** | There is **no separate browser-origin WebSocket device-auth** step like the standalone OpenClaw control UI; trust boundaries are the desktop process + local gateway configuration. |
| **Events** | Runtime events are delivered over **`blazeclaw.transport.event.v1`** (see `EventTransport.cpp`); `chat-events.js` maps `topic: chat.events` to **`blazeclaw.gateway.chat.events`**. |
| **How to confirm** | Breakpoint or log in `RouteGatewayRequest`; chat traffic never opens a user-managed WS URL unless you explicitly enable a shim path (below). |

### B. External OpenClaw gateway + optional `openclaw.ws.*` shim

| Aspect | Detail |
|--------|--------|
| **What it is** | Some builds/tests can target a **remote** OpenClaw-style gateway while still driving UI from the same WebView shell. Method names and auth scopes should match **OpenClaw canonical** RPC strings where possible. |
| **Auth / scopes** | Expect **device pairing**, **operator scopes**, and **reconnect** semantics closer to OpenClaw’s `GatewayBrowserClient` when this mode is active. |
| **Troubleshooting** | If events or RPC fail only in shim mode, compare **method spelling** against the mechanical audit and WebView `request("…")` strings. |

---

## RPC naming (WebView chat)

The embedded chat uses **OpenClaw-style canonical** names for session/model/skills bootstrap where the gateway exposes an alias:

| Capability | Canonical WebView method | Legacy / prefixed handler |
|------------|---------------------------|----------------------------|
| Session list | `sessions.list` | `gateway.session.list` |
| Session create | `sessions.create` | `gateway.sessions.create` |
| Model catalog | `models.list` | `gateway.models.list` |
| Slash skill commands | `skills.commands` | `gateway.skills.commands` |

`config.get` / `config.set` were already canonical. Chat core remains `chat.send`, `chat.history`, `chat.abort`, `agent.identity.get`.

Detached side-channel send is available from WebView slash command `/btw <message>`, which sends `chat.send` with `detached=true` and `deliver=true`; queued/sent acknowledgements are shown in the WebView detached notices side-channel panel (outside the main transcript).

Session controls now expose `sessions.subscribe` / `sessions.unsubscribe` and `sessions.compaction.list` / `sessions.compaction.branch` / `sessions.compaction.restore` in the WebView shell for parity testing, with lifecycle/session-reset refresh hooks and subscribed-state polling guards to keep the controls synchronized.

Exec approval baseline is now available in WebView chat: assistant text containing `approvalToken=...` is captured into an approval queue, and approve/deny actions execute `gateway.tools.call.execute` with `tool=email.schedule` and action payloads (`approve=true/false`).

Observability baseline is available in the WebView agents control-plane as a secured `Observability` tab (enable via `?observability=1` or localStorage `blazeclaw.observability.enabled=1`), wiring `gateway.health`, `gateway.health.details`, `gateway.transport.status`, `last-heartbeat`, `models.list`, and `gateway.logs.tail` with method-invoke debugging controls.

**Lifecycle parity (OpenClaw `server.impl.ts` Phase S0):** after `ServiceManager::WireGatewayCallbacks`, the gateway registers `gateway.parity.lifecycle`, returning a JSON payload with `contract` containing `GatewayParityLifecycleContract` (startup mode/source, flags, `transitionTrace`, auth-session generation counters). The same contract is embedded under `runtime.gatewayLifecycle.parityContract` in the operator diagnostics report from `ServiceManager::BuildOperatorDiagnosticsReport`.

Devices baseline is available in the WebView agents control-plane as a `Devices` tab, wiring `device.pair.list` with approve/reject/remove actions (`device.pair.approve`, `device.pair.reject`, `device.pair.remove`) for operator smoke coverage.

Skills-depth baseline is available in the WebView agents control-plane as a `Skills` panel surface, wiring `skills.search`, `skills.detail` (with `gateway.skills.info` fallback), `gateway.skills.install.execute` (with `skills.install` fallback), and `skills.update` for JSON edit/install smoke coverage.

Agent-files write baseline is available in the WebView agents control-plane as a `Files` panel surface, wiring `gateway.agents.files.list`, `gateway.agents.files.get`, and `gateway.agents.files.set` with editable content + save/reload controls (including optimistic rollback on save errors).

---

## Quick diagnostic checklist

1. **Which mode?** In-process vs external — see §A vs §B above.  
2. **RPC spelling:** Grep `web/chat/chat-controller.js` for `request("` and compare to dispatcher `Register("` in `blazeclaw/BlazeClawMfc/src/gateway/`.  
3. **Events:** Confirm `chat.events` topic is mapped in `chat-events.js` and that `CEventTransport` is emitting the expected topic.  
4. **Silent replies:** Assistant-only finals still honor `NO_REPLY` suppression (including cross-run finals) in `chat-events.js` + `chat-controller.js`.

## Embedded vs shim deltas (intentional)

- Embedded mode does not perform browser WebSocket hello/device-pair flows; routing is in-process via `RouteGatewayRequest`.
- External shim mode (`openclaw.ws.req` / `openclaw.ws.frame`) preserves OpenClaw-style request/response framing and handshake compatibility.
- Scope-related RPC failures in WebView now use `scope-errors.js` guidance (e.g. missing `operator.read`) so operator messaging stays aligned between modes.

## External shim smoke checklist

1. Confirm shim ready signal appears (`openclaw.ws.shim.ready`) before issuing requests.
2. Validate handshake path: `connect.challenge` then `connect` receives hello payload.
3. Run `chat.send` and `chat.events.poll` via shim and verify event envelope consistency (`delta` / `final` ordering).
4. Trigger a known scope failure (for example with reduced permissions) and verify WebView shows missing-`operator.read` guidance instead of generic request failure text.
5. Compare outcomes against embedded mode for the same method set (`chat.send`, `chat.history`, `sessions.list`) to isolate transport-only regressions.

---

## Changelog

| Date | Notes |
|------|--------|
| 2026-04-22 | Initial operator transport doc; aligned WebView chat bootstrap RPC names with gateway aliases. |
| 2026-04-22 | Phase D update: documented intentional embedded-vs-shim deltas, added external shim smoke checklist, and noted scope-error guidance parity. |
| 2026-04-22 | Phase F update: documented WebView exec-approval queue/actions using `gateway.tools.call.execute` for `email.schedule` tokens. |
| 2026-04-22 | Phase G update: documented secured WebView observability tab and its health/logs/debug RPC bindings. |
| 2026-04-22 | Phase H update: documented WebView devices tab baseline and `device.pair.*` action bindings. |
| 2026-04-22 | Phase I update: documented WebView skills-depth baseline with search/detail/install/update RPC bindings and fallback aliases. |
| 2026-04-22 | Phase J update: documented WebView agent-files write baseline with `gateway.agents.files.set` save/reload editing flow. |
