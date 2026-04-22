# OpenClaw Control UI controllers vs BlazeClaw UI (mechanical comparison)

**Date:** 2026-04-22  
**OpenClaw scope:** `openclaw/ui/src/ui/controllers/` (including `config/` helpers)  
**BlazeClaw scope:** Embedded WebView chat (`blazeclaw/BlazeClawMfc/web/chat/`), WebView bridge (`BlazeClawMfc/src/app/BlazeClawMFCView.cpp`), event fan-out (`BlazeClawMfc/src/app/EventTransport.cpp`), and native shells (MFC frames, settings dialogs) where they replace whole controller surfaces.

**Companion docs:** [`../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`](../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md) (RPC registrations), [`../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`](../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md) (in-process vs external gateway + WebView RPC naming), [`UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`](./UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md) (step-by-step procedure to close remaining gaps). This document focuses on **how UI collects prompts, issues gateway calls, and renders responses**.

---

## 1. End-to-end UI flow (summary)

### 1.1 OpenClaw Control UI

| Stage | Mechanism |
|-------|-----------|
| **Transport** | `GatewayBrowserClient` in `openclaw/ui/src/ui/gateway.ts` opens a **WebSocket** (or compatible transport) to the configured gateway URL, performs hello/auth, and exposes `request(method, params)` plus event callbacks. |
| **Prompt input** | Lit-rendered views (`openclaw/ui/src/ui/views/chat.ts` and helpers from `app-render.helpers.ts`) bind a textarea/state field `chatMessage` and attachment list on a large **multi-tab** host object assembled in `app-gateway.ts` / `app-render.ts`. |
| **Send path** | `app-chat.ts` normalizes slash commands (`executeSlashCommand`, `parseSlashCommand`), manages **send queue** and **tool stream** resets, then calls `sendChatMessage` from `controllers/chat.ts`, which optimistically appends a structured **user** message to `chatMessages` and invokes **`chat.send`** with `deliver: false`, `idempotencyKey` (UUID), optional image attachments (base64 in API shape). |
| **Streaming / completion** | Gateway pushes **`chat` event frames**; `app-gateway.ts` routes them to **`handleChatEvent`** in `controllers/chat.ts`, which updates `chatStream` for deltas, appends assistant messages on `final` / `aborted`, handles **cross-run** finals (sub-agent announce), **`NO_REPLY`** suppression, and `chat.abort`. UI re-renders from host state. |
| **History** | `loadChatHistory` → **`chat.history`** (limit 200), race-safe via request versioning on `ChatState`. |
| **Other surfaces** | Each domain controller (agents, config, cron, …) is imported by `app-render.ts` and wired to the same `client.request` pattern. |

### 1.2 BlazeClaw (MFC + WebView chat)

| Stage | Mechanism |
|-------|-----------|
| **Transport** | Chat page uses **`window.chrome.webview.postMessage`** with channel **`blazeclaw.gateway.rpc`** (`chat-controller.js`). C++ **`CBlazeClawMFCView::HandleWebMessageJson`** parses JSON and calls **`CBlazeClawMFCApp::RouteGatewayRequest`** — the gateway runs **in-process**, not as a separate browser-origin WebSocket client (unless using the optional `openclaw.ws.*` shim path in the same view). Operator-oriented topology and RPC naming: [`OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`](../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md). |
| **Prompt input** | `index.html` + **`chat-composer.js`**: `<textarea>` (`state.inputEl`), optional image attachments, session/model/thinking `<select>`s, slash-command hint menu. Drafts and **input history** (arrow recall) are **local to the WebView controller** (`draftsBySession`, `inputHistory`). |
| **Send path** | **`send()`** in `chat-controller.js`: local slash commands (`/help`, `/clear`, `/new`, `/session`, `/model`, `/thinking`, `/abort`) are handled in JS; otherwise **`chat.send`** with `sessionKey`, `deliver: false`, `idempotencyKey`, **`model`** and **`thinkingLevel`** (extra vs OpenClaw’s default `chat.send` payload in TS), `attachments`, optional `forceError`. User text is shown immediately as a simple **bubble** (`addMessage(..., "self")`), not as full structured content blocks. |
| **Streaming / completion** | C++ emits events through **`CEventTransport`** as **`blazeclaw.transport.event.v1`** with `topic: chat.events` (and legacy flat channels). **`chat-events.js`** maps topics to **`blazeclaw.gateway.chat.events`**, dedupes by seq/id, mirrors OpenClaw-style **cross-run final** handling, applies deltas via **`applyDeltaText`**, finalizes stream, and **`scheduleHistoryReconcile`** (debounced **`chat.history`** reload). **`blazeclaw.gateway.tools.lifecycle`** rows append to a simple tool timeline in HTML. |
| **History** | **`loadHistory`** → **`chat.history`**; thinking level may be updated from payload. |
| **Other surfaces** | BlazeClaw now ships a lightweight WebView control-plane in `web/chat` (agents/tools/files/skills/channels/cron/dreaming/nodes/instances/usage). Remaining major unported surfaces are **sessions**, **debug**, **devices**, **logs**, and **exec approvals** (plus full OpenClaw Lit UX depth). |

---

## 2. Mechanical audit: `openclaw/ui/src/ui/controllers/*`

Each row lists **primary gateway methods** (or HTTP bootstrap) and the **BlazeClaw counterpart** if any.

| File | Role | Key RPC / IO | BlazeClaw counterpart |
|------|------|--------------|------------------------|
| **`chat.ts`** | Chat history, send, abort, event reducer (`handleChatEvent`), `NO_REPLY` filtering, attachment encoding | `chat.history`, `chat.send`, `chat.abort` | `web/chat/chat-controller.js` + `chat-events.js` (partial behavioral match; message model simplified) |
| **`sessions.ts`** | Session list, subscribe, compaction checkpoints, patch/delete | `sessions.list`, `sessions.subscribe`, `sessions.compaction.*`, … | **Implemented baseline:** WebView chat uses canonical **`sessions.list`** / **`sessions.create`**, session subscription controls (`sessions.subscribe` / `sessions.unsubscribe`), and compaction controls (`sessions.compaction.list` / `sessions.compaction.branch` / `sessions.compaction.restore`) with lifecycle/session-reset refresh hooks and subscribed-state polling guards; deeper `sessions.ts` UX depth remains follow-up. |
| **`agents.ts`** | Agent list, tools catalog/effective, config save hooks | `agents.list`, `tools.catalog`, `tools.effective`, integrates with `config` | `web/chat/agents-controller.js` + `web/chat/index.html` (consumed via `createAgentsController`, tab lifecycle loaders, refresh hooks, and persistence wiring) |
| **`agent-files.ts`** | Workspace file list/get/set | `agents.files.list`, `agents.files.get`, `agents.files.set` | **Implemented baseline (WebView):** files panel now wires `gateway.agents.files.list` + `gateway.agents.files.get` + `gateway.agents.files.set` with selectable file list, editable content textarea, save/reload controls, optimistic save, and rollback-on-error handling |
| **`agent-identity.ts`** | Per-agent identity docs | gateway methods for identity | `web/chat/agents-controller.js` + `web/chat/index.html` (**focused parity implemented**: identity cache/loaders, selected-agent hydration, overview rendering, and regression coverage) |
| **`agent-skills.ts`** | Agent-scoped skills | `skills.status` (OpenClaw), skills state binding | `web/chat/agents-controller.js` + `web/chat/index.html` + gateway alias in `GatewayHost.Handlers.Runtime.ChatPipeline.cpp` (`skills.status` -> `gateway.skills.status`) with `agentSkillsReport`/`agentSkillsAgentId` parity semantics and regression coverage |
| **`assistant-identity.ts`** | Assistant display identity | reads snapshot / identity | `web/chat/chat-controller.js` + `chat-events.js` + `index.html` (**focused parity implemented for steps 1-8**: state/loaders/triggers/header binding/regression harness + validation gate) |
| **`channels.ts`** | Channel status, WhatsApp login flow | `channels.status`, `web.login.start`, `web.login.wait`, `channels.logout` | `web/chat/agents-controller.js` + `web/chat/index.html` + `GatewayHost.Handlers.Channels.cpp` (**focused parity implemented**: canonical `channels.status`, `web.login.start`, `web.login.wait`, and `channels.logout` are wired with OpenClaw-aligned channels state, snapshot-backed rendering, scope-aware status loading, runtime-consumed channels action controls (refresh/start/force-start/wait/logout), start/wait/logout busy-guard orchestration, post-action refresh semantics, config-refresh rehydration, and regression coverage) |
| **`channels.types.ts`** | Types for channels state | — | `web/chat/channels-state-contract.js` + `web/chat/agents-controller.js` (**field-level parity + hardening implemented**: JSDoc baseline/extension contract extraction, default-shape initializer centralization, and regression invariant coverage) |
| **`config.ts`** | Load/save/apply config, schema, update run | `config.get`, `config.schema`, `config.set`, `config.apply`, `update.run`, … | MFC **settings** / native dialogs; not the Lit config form |
| **`config/form-coerce.ts`**, **`form-utils.ts`** | Form value coercion for Zod-shaped config | — | Native-first config parsing (`SettingsDialog.cpp`, `ConfigLoader.cpp`) + WebView parity baseline completed in chat flow: canonical `config.get/set/schema` aliases (dispatcher + request-validator aligned), `web/chat/config-form-coerce.js`, gated submit-path coercion (`config.set`) and regression harness coverage |
| **`control-ui-bootstrap.ts`** | HTTP **`GET`** bootstrap JSON for assistant name/avatar before WS | Static asset path from `control-ui-contract` | `web/chat/chat-controller.js` bootstrap-shaped adapter (`getControlUiBootstrapConfig`) + lifecycle/session refresh routing with stale-response-safe `agent.identity.get` sequencing (`chat-events.js` triggers) |
| **`cron.ts`** | Full cron UI logic | `cron.*` methods | `web/chat/agents-controller.js` + `web/chat/index.html` Phase 1-5 parity baseline (`cron.status/list/add/update/remove/run/runs`, read+mutation workflows, validation/form controls, regression fixtures) + native parameter-aware `cron.*` contract-envelope handlers in `GatewayHost.Handlers.Runtime.Surface.cpp` |
| **`debug.ts`** | Operator debug panel | `status`, `health`, `models.list`, `last-heartbeat`, arbitrary method call | **Implemented baseline (secured WebView):** `observability` panel allows arbitrary method invoke with JSON params, plus last-heartbeat/model shortcuts; gated behind `?observability=1` / `localStorage blazeclaw.observability.enabled=1` |
| **`devices.ts`** | Device pairing | `device.pair.list`, `device.pair.approve`, `device.pair.reject`, `device.pair.remove` | **Implemented baseline (WebView):** `devices` tab in agents control-plane with pair inventory + approve/reject/remove actions via `agents-controller.js`; native pairing wizard depth remains follow-on |
| **`dreaming.ts`** | Dream diary / dreaming toggles | `doctor.memory.status`, `doctor.memory.dreamDiary`, `doctor.memory.backfillDreamDiary`, `doctor.memory.resetDreamDiary`, `doctor.memory.resetGroundedShortTerm`, `config.patch`, `config.schema.lookup` | `web/chat/agents-controller.js` + `web/chat/index.html` + `GatewayHost.Handlers.ConfigDiagnostics.cpp` (**Phase 1-4 implemented and revalidated**: OpenClaw-compatible status/diary contracts, schema-lookup + `config.get/config.patch` hash-aware dreaming flow parity, scene/diary/advanced Dreaming tab UX depth, runtime-backed persisted dreaming handler data/actions, and expanded gateway/WebView regressions) |
| **`exec-approval.ts`** | Inline exec approval queue UI | approval-related gateway traffic | **Implemented baseline in WebView:** `index.html` approval queue detects `approvalToken` from assistant text and routes approve/deny through `gateway.tools.call.execute` (`tool=email.schedule`) via `chat-controller.js` helper methods; native approval surface remains optional follow-on |
| **`exec-approvals.ts`** | Config form for exec approvals | config/exec methods | **Partial baseline:** runtime approval action wiring exposed in WebView (`executeExecApprovalAction`), but dedicated config/governance editor parity is still pending |
| **`health.ts`** | Health summary | `health` | **Implemented baseline (secured WebView):** dedicated observability summary from `gateway.health` + `gateway.health.details` + `gateway.transport.status` + `last-heartbeat` |
| **`logs.ts`** | Log tail viewer | `logs.tail` | **Implemented baseline (secured WebView):** `gateway.logs.tail` viewer with level filter, limit, pause/resume, and export snapshot |
| **`models.ts`** | Model catalog | `models.list` | **`models.list`** in `chat-controller.js` (dispatcher alias → `gateway.models.list`) |
| **`nodes.ts`** | Node list | `node.list` | `web/chat/agents-controller.js` + `web/chat/index.html` + `web/chat/chat-events.js` (**Phase 0 + 2-3 implemented**: nodes state slice (`nodesLoading`/`nodes`/`nodesError`), `loadNodes({ quiet? })` parity with guard/stale semantics against `node.list`, Nodes tab read-only surface + refresh wiring, reconnect lifecycle quiet-refresh + active-panel quiet polling orchestration, and WebView regression/doc hardening sync) |
| **`presence.ts`** | Multi-instance presence | `system-presence` | `web/chat/agents-controller.js` + `web/chat/index.html` + `GatewayHost.Handlers.Transport.cpp` (**Phase 0 + 2-3 implemented**: OpenClaw-compatible `system-presence` array payload semantics, presence state slice (`presenceLoading`/`presenceEntries`/`presenceError`/`presenceStatus`), `loadPresence({ quiet?, shouldIgnoreResponse? })` parity semantics, `Instances` tab read-only surface + refresh wiring, reconnect lifecycle quiet-refresh for active instances panel, and stale-panel suppression regression hardening) |
| **`scope-errors.ts`** | Shared copy for operator-scope errors | — | `web/chat/scope-errors.js` (shared utility; keep aligned as new panels land) |
| **`skills.ts`** | Skills report, edit, ClawHub search/install | `skills.*`, hub methods | **Implemented baseline (WebView):** agents `skills` panel now includes hub search (`skills.search`), detail (`skills.detail` with `gateway.skills.info` fallback), install (`gateway.skills.install.execute` with `skills.install` fallback), and JSON edit/update (`skills.update`) with busy/error guards; advanced ClawHub UX polish remains follow-up |
| **`usage.ts`** | Usage/cost/time series | `usage.*`, `sessions.usage.*` | `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.AgentSessionMutation.cpp` + `blazeclaw/BlazeClawMfc/src/gateway/GatewayHost.Handlers.RegistryIntrospection.cpp` + `blazeclaw/BlazeClawMfc/web/chat/agents-controller.js` + `blazeclaw/BlazeClawMfc/web/chat/index.html` (**Phase 0-4 implemented**: canonical `sessions.usage` compatibility alias + baseline usage endpoint envelopes, WebView usage state/loaders (`loadUsage`, detail loaders), `Usage` tab surface/actions, date-range controls, legacy date-interpretation fallback retry, and stale detail-response suppression with regression coverage) |
| **Test files (`*.test.ts`)** | Vitest coverage for controllers | — | No shared JS test harness for `web/chat` in same style |

---

## 3. Behavioral gaps (highest impact)

1. **Message transcript model** — OpenClaw keeps **`chatMessages`** as structured assistant/user objects (content blocks, timestamps). BlazeClaw chat renders **flattened text bubbles** and re-fetches history after terminal events; **tool / rich content** parity is weaker.
2. **Gateway client parity** — OpenClaw uses **device auth**, **scopes**, **reconnect** semantics, and **`GatewayBrowserClient`** feature flags. BlazeClaw WebView still uses a **narrow RPC bridge** for embedded mode, but Phase D now documents embedded-vs-shim intentional deltas and adds an external shim smoke checklist; scope-failure messaging is aligned in WebView via `scope-errors.js`.
3. **Method naming** — ~~BlazeClaw chat used prefixed `gateway.*` bootstrap names~~ **Addressed for chat bootstrap:** WebView now calls **`sessions.list`**, **`sessions.create`**, **`models.list`**, **`skills.commands`** with gateway dispatcher aliases; **`config.get` / `config.set`** were already canonical. Optional external clients may still use `gateway.*` spellings.
4. **Detached / side-channel messages** — OpenClaw **`sendDetachedChatMessage`** and **`/btw`** paths avoid showing user text in the main transcript; BlazeClaw now has `/btw` detached send (`chat.send` with `detached=true`, `deliver=true`), gateway-side user-turn transcript suppression, and a dedicated detached notices side-channel panel in WebView.
5. **Exec approvals / operator tools** — OpenClaw **`app-gateway.ts`** integrates **`exec-approval.ts`** with the host; BlazeClaw now provides a WebView baseline approval queue (token detect + approve/deny action) for `email.schedule`, while richer policy/config surfaces remain follow-up.
6. **Control-plane breadth/depth** — BlazeClaw now has WebView tabs for **agents/channels/cron/dreaming/nodes/instances/usage/files/skills/observability/devices**, but these are intentionally lightweight and still behind OpenClaw’s Lit control-plane depth (especially deeper **debug/logs/devices** UX, richer **exec approvals/config** governance, and richer settings/config UX).

---

## 4. Suggestions to close the gap (status 2026-04-22)

Original suggestions are tracked in [`UI_CONTROLLERS_GAP_SUGGESTIONS_IMPLEMENTATION_TRACKER.md`](./UI_CONTROLLERS_GAP_SUGGESTIONS_IMPLEMENTATION_TRACKER.md). Current status:

**Phase A sequencing baseline:** completed and frozen in [`UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`](./UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md) (priority stack, dual-lane chat semantics automation, B-F destination confirmation).

1. **RPC naming drift** — **Done** for embedded chat bootstrap: canonical **`sessions.list`**, **`sessions.create`**, **`models.list`**, **`skills.commands`** in `chat-controller.js` plus gateway aliases and request-validator entries. See [`OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`](../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md).
2. **Chat event semantics** — **Documented + tightened:** cross-run finals respect **`NO_REPLY`**; terminal final/aborted paths now commit structured transcript entries directly; parity matrix in [`CHAT_EVENTS_SEMANTICS_MATRIX.md`](./CHAT_EVENTS_SEMANTICS_MATRIX.md). Golden OpenClaw fixture replay remains optional follow-up.
3. **Structured transcript** — **Phase B completed:** structured schema widened (`id`, `sessionKey`, `runId`, `source`, `terminalState`) with unified writer paths across history/addMessage/stream finalization; `scheduleHistoryReconcile` is now used as repair for missing terminal text rather than primary merge. Transcript-driven rendering is available behind feature flag (`?structuredTranscript=1` or localStorage `blazeclaw.chat.structuredTranscript=1`) with fallback to bubble path when disabled.
4. **Incremental controller ports** — **Deferred** (product scope): next increments listed in [`UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`](./UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md) (sessions UI, debug, devices, logs, exec approvals).
5. **Transport operator docs** — **Done:** [`OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`](../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md).

---

## 5. Implemented vs Pending (fast audit matrix)

| Area | Implemented now | Pending / follow-up |
|------|------------------|---------------------|
| **Chat transport + stream** | `chat.send/history/abort`, stream delta handling, assistant identity bootstrap adapter, canonical bootstrap RPCs (`sessions.list`, `sessions.create`, `models.list`, `skills.commands`), structured transcript schema + deterministic stream-final commits (`getStructuredTranscript`), transcript-driven render flag path, repair-only history reconcile for missing terminal text, stable degraded text rendering for `image`/`tool-call`/`tool-result` blocks, detached `/btw` send path (`detached=true`, `deliver=true`) with detached user-message suppression from transcript/history plus detached notices side-channel UI, scope-aware RPC error formatting (`scope-errors.js`), session subscribe/compaction controls in WebView with lifecycle/session-reset refresh + subscribed polling guards, and actionable exec-approval queue for `email.schedule` tokens (`gateway.tools.call.execute`) | Richer content-block parity beyond current degraded text markers |
| **Agents control-plane shell** | Multi-panel shell in `web/chat` (`overview/tools/files/skills/channels/cron/dreaming/nodes/instances/usage`) with panel persistence and regressions | Polish-only: denser list ergonomics, richer empty/loading states, and consistency pass on action affordances |
| **Agent identity + skills** | `agent.identity.get` lifecycle wiring; `skills.status`/`gateway.skills.status`; commands + search/detail/install/update actions surfaced in WebView skills panel | Advanced skills UX polish (catalog richness, richer form editing ergonomics) |
| **Agent files** | `gateway.agents.files.list/get/set` wired in WebView with selectable-file editing + save/reload and optimistic rollback behavior | Richer file ops UX (diffing/conflict UX, create/delete ergonomics, larger-file tooling) |
| **Channels** | Canonical `channels.status`, `web.login.start`, `web.login.wait`, `channels.logout`; snapshot state + action lifecycle | Polish-only: per-channel diagnostics presentation, action-history visibility, and copy/label refinement |
| **Cron** | `cron.status/list/add/update/remove/run/runs`, form validation, read/mutation loops | Polish-only: expanded validation hints, run history drilldown ergonomics, and mutation feedback clarity |
| **Dreaming** | `doctor.memory.status/dreamDiary/backfill/reset*`, `config.patch`, `config.schema.lookup`, dreaming panel flows | Polish-only: advanced-scene readability, diary paging ergonomics, and optional payload-depth formatting refinements |
| **Nodes + Presence + Usage** | `node.list`, `system-presence`, `sessions.usage*` tabs, loaders, stale guards, regressions | Polish-only: richer per-row diagnostics, detail drilldown affordances, and chart/filter UX improvements |
| **Config utility parity** | `config/form-coerce` + `config/form-utils` utility parity for current WebView scope | Full standalone schema-driven WebView config form (if product scope changes) |
| **Still missing controller surfaces** | N/A | full `exec-approvals.ts` config surface, and deeper `devices.ts` / `debug.ts` / `logs.ts` / `sessions.ts` / `exec-approval.ts` UX parity |

### Deep-dive references (when matrix rows need detail)

- Decision tracker: [`UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`](./UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md)
- Per-controller deep dives under `docs/compare/`:
  - `agents.ts`, `agent-identity.ts`, `agent-skills.ts`
  - `channels.ts`, `channels.types.ts`
  - `cron.ts`, `dreaming.ts`, `nodes.ts`, `presence.ts`, `usage.ts`
  - `control-ui-bootstrap.ts`, `config/form-coerce.ts`, `config/form-utils.ts`
  - [`CHAT_EVENTS_SEMANTICS_MATRIX.md`](./CHAT_EVENTS_SEMANTICS_MATRIX.md), [`OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`](../../blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md)

## 6. Primary file map (quick navigation)

| OpenClaw | BlazeClaw |
|----------|-----------|
| `openclaw/ui/src/ui/controllers/chat.ts` | `blazeclaw/BlazeClawMfc/web/chat/chat-controller.js`, `chat-events.js` |
| `openclaw/ui/src/ui/app-chat.ts`, `app-gateway.ts` | `chat-composer.js`, `BlazeClawMfc/src/app/BlazeClawMFCView.cpp` (RPC + shim), `EventTransport.cpp` |
| `openclaw/ui/src/ui/gateway.ts` | In-process routing + optional WS shim in `BlazeClawMFCView.cpp` |
| `openclaw/ui/src/ui/views/chat.ts` | `blazeclaw/BlazeClawMfc/web/chat/index.html` + inline styles |

---

*This audit is based on static review of the listed trees as of the document date; re-run diffs when either side’s UI or gateway protocol changes materially.*

Last updated: 2026-04-22 (gap suggestions 1–3/5 implemented baseline: canonical chat bootstrap RPCs + gateway aliases, `NO_REPLY` on cross-run finals, structured transcript schema/writer hardening + deterministic stream-final commit + repair-only reconcile guard + transcript-render flag path + degraded rich-content text markers, detached `/btw` path end-to-end with user-turn transcript suppression + detached notices side-channel UI, Phase D gateway parity baseline docs/checklist + scope-aware RPC error messaging, Phase E sessions baseline completed including subscription/compaction controls plus lifecycle/session-reset refresh and subscribed polling guards, **Phase F exec-approval baseline implemented** with approval-token queue + approve/deny action routing via `gateway.tools.call.execute`, **Phase G observability baseline implemented** with secured WebView debug/logs/health surface (`gateway.health`/`gateway.health.details`/`gateway.transport.status`/`last-heartbeat`/`gateway.logs.tail`), **Phase H devices baseline implemented** with `device.pair.list` + approve/reject/remove controls in WebView devices tab and regression coverage, **Phase I skills-depth baseline implemented** with WebView search/detail/install/update actions and regression coverage, **Phase J agent-files write baseline implemented** with WebView save/reload editing flow over `gateway.agents.files.set` and optimistic rollback semantics, **Phase K control-plane depth/polish triage completed** with §5 pending rows reduced to explicit polish-only bullets for agents/channels/cron/dreaming/nodes/presence/usage; suggestion 4 remains scoped to Phase0 tracker; Phase A sequencing baseline committed in execution plan + companion trackers)
