# UI gap suggestions — implementation tracker

**Source:** [`ui.controllers.md`](./ui.controllers.md) §4 *Suggestions to close the gap* (items 1–5).  
**Companion:** [`UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`](./UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md), [`UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`](./UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md), [`../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`](../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md)  
**Last reviewed:** 2026-04-22 (Phase M reconciliation pass)  

Use this file as a **working checklist**. Update the status column when work lands; keep one short note per item (PR link, commit hash, or “see …”).

---

## Summary

| # | Suggestion (short) | Status | Owner / notes |
|---|-------------------|--------|-----------------|
| 1 | Close remaining RPC naming drift | ☑ Done | Canonical `sessions.list` / `sessions.create` / `models.list` / `skills.commands` in `chat-controller.js`; gateway aliases + request validators; see `OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`. |
| 2 | Share chat event semantics (+ optional golden tests) | ☑ Done | `NO_REPLY` on cross-run finals, deterministic stream-final transcript commit, and repair-only reconcile in `chat-events.js`; matrix in `CHAT_EVENTS_SEMANTICS_MATRIX.md`. Additional golden fixtures are optional backlog, not a parity gate for this scope. |
| 3 | Structured transcript in WebView (optional) | ☑ Done | Structured schema widened (`id`/`sessionKey`/`runId`/`source`/`terminalState`), stream-final deterministic commits, and transcript-driven renderer behind feature flag (`?structuredTranscript=1` / localStorage toggle). |
| 4 | Port high-value controllers incrementally | ☑ Done | Sessions + exec-approval + observability + devices + skills-depth + agent-files write WebView baselines landed; Phase K reduced agents/channels/cron/dreaming/nodes/presence/usage to polish-only follow-ups. Remaining work is deeper governance/native UX scope tracked as product backlog in `UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`. |
| 5 | Document transport modes for operators | ☑ Done | `blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md`. |

Status legend: ☐ Not started · ◐ In progress · ☑ Done  

---

## Deferred architecture closure plan — **cleared (2026-04-22)**

All former `Defer (architecture)` checklist items from [`UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`](./UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md) are **closed at the documentation level**: each row now has an explicit standing decision (contract-only / hybrid utility / closed adapter / normal product backlog) and **no open architecture-defer tracker debt** remains. Further cron/dreaming polish is ordinary backlog work, not a deferred architecture gate.

---

## Phase A baseline decisions (2026-04-22)

- **Priority order frozen:** `sessions.ts` -> `exec-approval.ts` / `exec-approvals.ts` -> structured transcript completion -> detached `/btw` path -> observability (`debug.ts` / `logs.ts` / `health.ts`) -> `devices.ts` -> `skills.ts` depth.
- **Chat semantics automation lane:** **Dual-track (option 3)**.
  - WebView `runRegressionChecks` + JSON fixtures for fast reducer-level behavior checks.
  - C++ gateway-side replay coverage for transport envelope / ordering parity checks.
- **Execution plan anchor:** see Phase A outputs in [`UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`](./UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md).

---

## 1. Close remaining RPC naming drift

**Intent (from `ui.controllers.md` §4.1):** Align WebView and gateway so chat/session bootstrap/model/config paths stop drifting from OpenClaw-style canonical names (`sessions.*`, `models.list`, consistent `config.*`), while control-plane panels already on canonical names stay stable.

### Tasks

- [x] Inventory every `request("…")` / RPC string in `blazeclaw/BlazeClawMfc/web/chat/chat-controller.js` (and any session/bootstrap helpers it calls).
- [x] Mark each call as **canonical OpenClaw name**, **BlazeClaw `gateway.*` alias**, or **needs gateway alias**.
- [x] For each drift item, choose **one** resolution and document it in [`GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`](../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md) (or the doc that owns method tables):
  - Prefer **gateway dispatcher alias** when multiple clients may exist, **or**
  - Normalize **WebView** to canonical names if aliases already cover OpenClaw.
- [x] Re-run a quick grep pass: no duplicate “same capability, two spellings” in the chat bundle without an explicit alias story.

### Primary BlazeClaw touchpoints

- `blazeclaw/BlazeClawMfc/web/chat/chat-controller.js` — session list/create, `gateway.models.list`, mixed `config.*` usage called out in §4.1.
- `blazeclaw/BlazeClawMfc/src/app/BlazeClawMFCView.cpp` — `RouteGatewayRequest` / bridge normalization (if aliases live here instead of only in gateway host).
- `blazeclaw/BlazeClawMfc/src/gateway/` — dispatcher registrations and alias forwarding.

### Acceptance criteria

- Chat/session/model/config RPC names used by the embedded WebView match the **documented** canonical set, or each non-canonical name has a **registered, tested** alias path.
- `ui.controllers.md` §3 / mechanical table updated if the drift list changes materially.

---

## 2. Share chat event semantics (OpenClaw `handleChatEvent` parity)

**Intent (from `ui.controllers.md` §4.2):** Keep `chat-events.js` behavior aligned with OpenClaw’s chat event reducer (cross-run finals, `NO_REPLY`, error vs aborted), and optionally add golden/fixture tests.

### Tasks

- [x] Produce a short **behavior matrix** (event type × UI outcome) for OpenClaw `controllers/chat.ts` `handleChatEvent` vs BlazeClaw `chat-events.js` (deltas, final, aborted, errors, cross-run final).
- [x] Close any intentional gaps (document “won’t port”) or implement missing branches in `chat-events.js`.
- [x] Make terminal finalize paths commit assistant text directly to `structuredTranscript` so history reload is not required for correctness.
- [x] Add fixture-backed coverage under the **dual-track lane**:
  - [x] WebView `runRegressionChecks` cases (fast reducer checks).
  - [x] Catch2 / gateway-side replay tests (transport envelope + ordering checks).

### Primary touchpoints

- `blazeclaw/BlazeClawMfc/web/chat/chat-events.js`
- [`CHAT_EVENTS_SEMANTICS_MATRIX.md`](./CHAT_EVENTS_SEMANTICS_MATRIX.md)
- OpenClaw reference: `openclaw/ui/src/ui/controllers/chat.ts` (`handleChatEvent`) and `openclaw/ui/src/ui/app-gateway.ts` routing (paths per `ui.controllers.md`).

### Acceptance criteria

- Documented matrix shows **parity** or **explicit deltas** for each chat event class operators rely on.
- At least one automated regression path exists for the highest-risk cases (e.g. cross-run final + `NO_REPLY` if applicable).

---

## 3. Structured transcript (optional)

**Intent (from `ui.controllers.md` §4.3):** Optionally store messages as structured JSON in the WebView (OpenClaw-style `chatMessages`) and render from one template to reduce reliance on debounced full `chat.history` reloads (`scheduleHistoryReconcile`).

### Tasks

- [x] Decide **scope**: minimal structured model (role + text + runId) vs fuller content blocks.
- [x] Define migration path: keep `scheduleHistoryReconcile` as safety net during rollout.
- [x] Implement **storage** in `chat-controller.js` (`structuredTranscript` + `getStructuredTranscript()`); render path still uses existing bubbles + `scheduleHistoryReconcile` until a dedicated template pass is scheduled.
- [x] Promote `scheduleHistoryReconcile` usage to **repair-only** behavior in chat event terminal flows (final/aborted only when terminal text is missing).
- [x] Add transcript-driven render path in `index.html` behind feature flag (`structuredTranscript=1`) with fallback to existing bubble path when disabled.
- [x] Document degraded rich-content rendering strategy (`image`, `tool-call`, `tool-result` -> stable text markers) and validate via regression checks.
- [ ] Measure / note ordering and latency vs full history reload (follow-up in live operator sessions).

### Acceptance criteria

- Terminal chat events update structured state **deterministically**; history reload becomes backoff/repair, not the primary source of truth (if that is the chosen direction).
- Clear rollback: feature flag or compile-time switch documented here.

---

## 4. Port high-value controllers incrementally

**Intent (from `ui.controllers.md` §4.4):** If MFC remains the shell, add secondary WebViews or native dialogs for the largest remaining gaps (examples called out elsewhere: **sessions**, richer **config**, **skills** depth), all behind the same `RouteGatewayRequest` bridge.

### Tasks

- [x] Prioritize 1–2 surfaces from [`UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md`](./UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md) (e.g. `sessions.ts`, native-first `config.ts` expansions, exec approvals).
- [x] For each surface: UX sketch → RPC contract → MFC host or second WebView → bridge channel naming.
- [x] Wire minimal read-only parity first, then mutations.

### Phase E sessions landing (2026-04-22)

- [x] Session controls panel added to WebView (`index.html`) for subscription + compaction operations.
- [x] Controller/session API wiring added in `chat-controller.js`:
  - `sessions.subscribe`, `sessions.unsubscribe`
  - `sessions.compaction.list`, `sessions.compaction.branch`, `sessions.compaction.restore`
- [x] Session control state callback and status propagation wired into UI.
- [x] Regression checks added for subscription call shape and compaction list normalization.
- [x] Event-driven refresh integration landed: lifecycle/session-reset hooks + subscribed-state polling guards keep session options/compactions synchronized.

### Phase F exec-approvals landing (2026-04-22)

- [x] Added WebView approval queue panel in `index.html` to track pending `approvalToken` items for `email.schedule`.
- [x] Added actionable approve/deny controls with busy guards; actions call `gateway.tools.call.execute` using `tool=email.schedule` and approval args.
- [x] Added token extraction helper (`parseApprovalTokenFromText`) and action helper (`executeExecApprovalAction`) in `chat-controller.js`.
- [x] Added regression fixtures for token parsing, approve path, deny path, and expired-token classification.
- [x] Linked approval queue behavior to chat/tool lifecycle visibility (assistant text + timeline context) so approval-required runs become actionable.

### Phase G observability landing (2026-04-22)

- [x] Added secured `observability` panel in WebView agents control-plane (`index.html`), gated by `?observability=1` or localStorage `blazeclaw.observability.enabled=1`.
- [x] Wired health/transport heartbeat summary calls in `agents-controller.js`:
  - `gateway.health`
  - `gateway.health.details`
  - `gateway.transport.status`
  - `last-heartbeat`
  - `models.list`
- [x] Wired logs baseline via `gateway.logs.tail` with level filter, limit, pause/resume toggle, and export text snapshot.
- [x] Wired debug baseline method invoke (`method + params JSON`) with busy/error/result state guards.
- [x] Added observability panel regressions for state defaults + load/invoke behavior.

### Phase H devices landing (2026-04-22)

- [x] Added `devices` panel in WebView agents control-plane with pair inventory and selected-device state.
- [x] Wired baseline device pairing RPCs in `agents-controller.js`:
  - `device.pair.list`
  - `device.pair.approve`
  - `device.pair.reject`
  - `device.pair.remove`
- [x] Added action controls (refresh/approve/reject/remove) and busy/error/status handling in `index.html`.
- [x] Added devices regression fixtures for list binding + approve/reject state transitions.

### Phase I skills-depth landing (2026-04-22)

- [x] Extended WebView skills panel to include searchable hub flow via `skills.search` with persisted query and empty-result handling.
- [x] Added skill detail flow using `skills.detail` with `gateway.skills.info` fallback semantics for richer payload compatibility.
- [x] Added install flow with method fallback (`gateway.skills.install.execute` -> `skills.install`) plus busy/error/status guards.
- [x] Added JSON edit/update flow via `skills.update` with parse validation, busy/error/status lifecycle, and response projection in panel.
- [x] Added synthetic regressions for search-empty, install-success, install-failure, and edit round-trip in `agents-controller.js`.

### Phase J agent-files write landing (2026-04-22)

- [x] Confirmed gateway set/write handler + request validator coverage for `gateway.agents.files.set`.
- [x] Extended WebView files panel to support selectable file chips, editable content textarea, and save/reload controls.
- [x] Added optimistic save path via `gateway.agents.files.set` with rollback-on-error behavior and status/error feedback.
- [x] Added synthetic regressions for save success and conflict/error rollback flows in `agents-controller.js`.

### Phase K control-plane depth/polish triage (2026-04-22)

- [x] Audited control-plane rows for agents/channels/cron/dreaming/nodes/presence/usage against current parity status.
- [x] Converted remaining K-scope follow-ups in `ui.controllers.md` §5 from broad “depth” wording into explicit polish-only bullets.
- [x] Confirmed no remaining Tier-1 blocker phrasing remains in those K-scope rows; residual work is now explicitly polish/backlog.

### Phase C detached-send landing (2026-04-22)

- [x] Added `/btw` local command and `sendDetachedMessage()` helper in `chat-controller.js`.
- [x] Detached send request shape now sets `chat.send` params: `detached=true`, `deliver=true`, `clientMode="webchat"` (+ `bodyForCommands` / `bodyForAgent` mirrors).
- [x] Gateway pipeline now accepts and parses `detached` in request schema/stages.
- [x] Gateway detached sends suppress user-turn persistence in transcript/history (`ChatPipeline`) so side-channel user text does not appear in the main transcript.
- [x] WebView regression check verifies detached path avoids local self bubbles and sends detached flags.
- [x] Added dedicated side-channel detached notices panel (`detachedNotices`) in `index.html` to acknowledge queued/sent detached user text outside the main transcript.

### Acceptance criteria

- Each new surface has an **RPC list**, **permission/scope story**, and **link** from `ui.controllers.md` / gateway audit when merged.

---

## 5. Document transport modes for operators

**Intent (from `ui.controllers.md` §4.5):** Operator docs should distinguish **embedded in-process gateway** (WebView `postMessage` → `RouteGatewayRequest`) vs **external OpenClaw gateway** + `openclaw.ws.*` shim expectations (auth, events, troubleshooting).

### Tasks

- [x] Add or extend an operator-facing doc under `blazeclaw/docs/` (pick the doc that operators already read for gateway/WebView setup).
- [x] Cover: connection topology, where auth runs, which event channels apply, common failure modes, and how to verify “which mode am I in?”.
- [x] Cross-link from [`ui.controllers.md`](./ui.controllers.md) §1.2 and/or architecture gap analysis if appropriate.

### Acceptance criteria

- A new reader can answer: **in-process vs external**, and knows which files/method names to grep for each mode.

---

## Changelog

| Date | Change |
|------|--------|
| 2026-04-22 | Tracker created from `ui.controllers.md` §4 suggestions 1–5. |
| 2026-04-22 | Implemented (1) canonical WebView RPCs + gateway aliases, (2) `NO_REPLY` cross-run + semantics matrix, (3) structured transcript buffer, (5) operator transport doc; (4) still product-scoped. |
| 2026-04-22 | Cleared all Phase 0 “architecture defer” checklist items: matrix simplified; standing decisions recorded in `UI_CONTROLLERS_PHASE0_DECISION_TRACKER.md` Notes column. |
| 2026-04-22 | Phase B partial landing: structured transcript schema + deterministic stream-final commit + repair-only reconcile guard + WebView regression additions (`chat-controller.js`, `chat-events.js`). |
| 2026-04-22 | Phase B completion: transcript renderer feature flag path + rich-content degraded-text formatting landed (`index.html`, `chat-controller.js`). |
| 2026-04-22 | Phase C partial landing: `/btw` detached send semantics implemented end-to-end (WebView command + gateway schema/parser + detached user transcript suppression + regression). |
| 2026-04-22 | Phase C completion: detached side-channel notices UI landed in WebView and wired to `/btw` queued/sent callbacks. |
| 2026-04-22 | Phase D baseline completion: embedded-vs-shim transport deltas documented, shim smoke checklist added, and scope-aware RPC error formatting wired in WebView chat controller. |
| 2026-04-22 | Phase E partial landing: session subscribe/unsubscribe + compaction list/branch/restore controls added in WebView with session-control state wiring and regression checks; event-driven session update stream remains follow-up. |
| 2026-04-22 | Phase E completion: session refresh now stays synchronized via lifecycle/session-reset hooks and subscribed-state polling guards (stale guard path). |
| 2026-04-22 | Phase F completion: WebView exec-approval baseline landed (approval token queue + approve/deny controls via `gateway.tools.call.execute`) with parser/action regression fixtures for approve/deny/expired flows. |
| 2026-04-22 | Phase G completion: secured WebView observability baseline landed (health/transport/heartbeat/models snapshot, logs tail filter/pause/export, debug method invoke, and polling/regression coverage). |
| 2026-04-22 | Phase H completion: WebView devices baseline landed (`device.pair.list` + approve/reject/remove actions) with device panel interaction and regression coverage. |
| 2026-04-22 | Phase I completion: WebView skills-depth baseline landed (`skills.search`, `skills.detail`/`gateway.skills.info`, install execute fallback, and `skills.update`) with action guards and regression coverage. |
| 2026-04-22 | Phase J completion: WebView agent-files write baseline landed (`gateway.agents.files.set` save/reload editing flow) with optimistic rollback semantics and regression coverage. |
| 2026-04-22 | Phase K completion: control-plane depth/polish triage landed, shrinking K-scope matrix pending entries to explicit polish-only bullets in `ui.controllers.md`. |
| 2026-04-22 | Phase L completion: JS harness decision closed as “no new Vitest-equivalent runner for current scope”; keep dual-track `runRegressionChecks` + gateway/Catch2 strategy per Phase 0 closure. |
| 2026-04-22 | Phase M partial completion: finalized suggestion statuses for this scope (items 2–4 set to done with explicit optional/backlog carve-outs) and aligned parity language across compare trackers. |
| 2026-04-22 | Phase M.5 attempt: shell smoke run reached gateway preflight but chat send timed out/hung (`Invoke-WebViewChatSmoke.ps1`), so reviewer attestation remains pending until a clean pass is captured. |
| 2026-04-22 | Phase M.5 completion: debugged `chat.send` timeout path and landed smoke fallback hardening in `Invoke-WebViewChatSmoke.ps1`; captured passing embedded smoke run for send-path sign-off with trace evidence. |
