# UI Controllers Parity — Phase 0 Decision Tracker

**Date:** 2026-04-22 (architecture defer checklist **cleared** — see matrix “Notes”)  
**Source baseline:**
- [`ui.controllers.md`](./ui.controllers.md)
- [`../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`](../../blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md)
- **Procedure to close gaps:** [`UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`](./UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md)

## Phase 0 objective

Freeze an implementation decision for each OpenClaw UI controller file:
- **Port to WebView JS**
- **Port to MFC/C++**
- **Defer**

## Decision rules used

1. Prefer **WebView JS** when behavior is controller-heavy and already aligns with `web/chat/*` patterns.
2. Prefer **MFC/C++** when capability is fundamentally native-shell/config-dialog/workspace-integrated.
3. Mark **Defer** when the surface is not needed for near-term operator parity or requires a larger product-scope tab/shell decision.

## File-level decisions (implemented vs pending matrix)

| OpenClaw controller file | Decision destination | Current status (short) | Notes |
|---|---|---|---|
| `chat.ts` | WebView JS | Implemented baseline (`chat.send/history/abort`, stream handlers) | Product backlog: detached send + richer transcript parity |
| `sessions.ts` | WebView JS | Not implemented as OpenClaw controller-equivalent | Product backlog: `sessions.list/subscribe/compaction` UI |
| `agents.ts` | WebView JS | Implemented baseline multi-tab shell | Product backlog: Lit-depth polish |
| `agent-files.ts` | WebView JS + MFC/C++ | Partial (`gateway.agents.files.list/get`) | Product backlog: `set` + deeper file UX |
| `agent-identity.ts` | WebView JS | Focused parity implemented | Optional polish |
| `agent-skills.ts` | WebView JS | Focused parity implemented | Product backlog: advanced skill management |
| `assistant-identity.ts` | WebView JS | Focused parity implemented | Optional polish |
| `channels.ts` | WebView JS | Focused parity implemented (`channels.status`, login wait/start/logout) | Optional channel-specific depth |
| `channels.types.ts` | WebView JS (contract-only) | Field-level contract parity implemented | **Architecture closure cleared (2026-04-22):** `channels-state-contract.js` is canonical; no TS framework-port required for current scope |
| `config.ts` | MFC/C++ | Native-first path active | Optional standalone WebView config form remains product decision |
| `config/form-coerce.ts` | WebView utility (hybrid; architecture closed) | Utility parity implemented for current WebView scope | **Architecture closure cleared (2026-04-22):** coercion stays on live WebView mutation paths; native owns full config surface |
| `config/form-utils.ts` | WebView utility (architecture closed) | Utility parity implemented for current WebView scope | **Architecture closure cleared (2026-04-22):** parity + guards + regressions sufficient; no full OpenClaw port in current scope |
| `control-ui-bootstrap.ts` | WebView adapter (architecture closed) | Bootstrap-shaped WebView adapter implemented | **Architecture closure cleared (2026-04-22):** semantic parity via adapter + operator doc; HTTP bootstrap transport intentionally not mirrored |
| `cron.ts` | WebView JS (architecture closed) | Phase baseline implemented (`cron.status/list/add/update/remove/run/runs`) | **Architecture closure cleared (2026-04-22):** further work is normal hardening backlog, not open “defer architecture” debt |
| `debug.ts` | Secured WebView baseline + MFC/C++ follow-on | WebView observability baseline implemented (method invoke + heartbeat/models shortcuts) | Native diagnostics depth remains optional follow-on |
| `devices.ts` | WebView baseline + MFC/C++ follow-on | WebView devices tab baseline implemented (`device.pair.list` + approve/reject/remove actions) | Native pairing wizard depth remains optional follow-on |
| `dreaming.ts` | WebView JS (architecture closed) | Phase baseline implemented (`doctor.memory.*` + config hooks) | **Architecture closure cleared (2026-04-22):** deltas vs OpenClaw are product polish backlog, not open architecture defer |
| `exec-approval.ts` | WebView JS (Phase F bridge) + MFC/C++ follow-on | WebView baseline implemented (token queue + approve/deny actions) | Native approval UX still preferred for deeper policy workflows |
| `exec-approvals.ts` | WebView JS (Phase F bridge) + MFC/C++ follow-on | WebView baseline implemented via runtime approval action wiring | Native governance/config surface remains follow-on |
| `health.ts` | Secured WebView baseline + MFC/C++ follow-on | WebView observability health summary implemented (`gateway.health`, `gateway.health.details`, `gateway.transport.status`) | Native health surface remains optional follow-on |
| `logs.ts` | Secured WebView baseline + MFC/C++ follow-on | WebView observability logs tail baseline implemented (`gateway.logs.tail` with filter/pause/export controls) | Native log tail depth remains optional follow-on |
| `models.ts` | WebView JS | Implemented (`models.list` in chat flow; alias → `gateway.models.list`) | Optional catalog depth |
| `nodes.ts` | WebView JS | Baseline implemented (`node.list`, nodes tab) | Richer node operations backlog |
| `presence.ts` | WebView JS | Baseline implemented (`system-presence`, instances tab) | Optional UX depth |
| `scope-errors.ts` | WebView JS | Implemented shared scope error utility | Keep coverage as new panels land |
| `skills.ts` | WebView JS | Partial (`skills.status`/report + commands) | Product backlog: edit/install/ClawHub |
| `usage.ts` | WebView JS | Baseline implemented (`sessions.usage*`) | Analytics depth backlog |
| `*.test.ts` | Harness strategy (architecture closed) | Targeted WebView / gateway regressions in-repo | **Architecture closure cleared (2026-04-22):** no Vitest parity harness required for current scope; revisit if full OpenClaw-style suite becomes a product requirement |

Quick drill-down references:
- [`docs/compare/agents.ts/AGENTS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md`](./agents.ts/AGENTS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md)
- [`docs/compare/channels.ts/CHANNELS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md`](./channels.ts/CHANNELS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md)
- [`docs/compare/cron.ts/CRON_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md`](./cron.ts/CRON_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md)
- [`docs/compare/dreaming.ts/DREAMING_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md`](./dreaming.ts/DREAMING_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md)
- [`docs/compare/usage.ts/USAGE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md`](./usage.ts/USAGE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md)

## Phase 0 status

- Baseline inventory frozen: ✅
- File-by-file destination decisions frozen: ✅
- Architecture defer checklist: ✅ **cleared (2026-04-22)** (former `Defer (architecture)` rows are now explicit closed decisions in the matrix “Notes” column)
- Execution: ongoing as **normal product backlog** (sessions UI, native diagnostics, richer transcript, etc.) — no open “architecture defer” tracker debt
- Phase A baseline + sequencing (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed (priority stack + automation lane + B-F destination freeze)
- Phase B transcript foundation (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed (schema + single-writer + repair-only reconcile + transcript renderer feature flag + degraded rich-content text policy + WebView regressions)
- Phase C detached send semantics (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed (`/btw` command + detached `chat.send` params + gateway detached-user transcript suppression + regression + detached side-channel notices UI)
- Phase D gateway client parity baseline (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed for this scope (embedded-vs-shim delta documentation, external shim smoke checklist, scope-aware RPC error messaging via `scope-errors.js`)
- Phase E sessions parity (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed (WebView session subscribe/unsubscribe + compaction list/branch/restore controls, lifecycle/session-reset refresh handling, subscribed-state polling guards, and regression coverage)
- Phase F exec approvals parity (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed for baseline scope (WebView approval queue detects `approvalToken`, approve/deny actions call `gateway.tools.call.execute` for `email.schedule`, and token parser/approve-deny-expired regression fixtures added)
- Phase G observability parity (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed for baseline scope (secured WebView observability tab with `gateway.health`/`gateway.health.details`/`gateway.transport.status`/`last-heartbeat`, logs tail filter/pause/export, and debug method invoke controls)
- Phase H devices parity (from `UI_CONTROLLERS_PARITY_CLOSURE_EXECUTION_PLAN.md`): ✅ completed for baseline scope (WebView devices tab with `device.pair.list` inventory + approve/reject/remove actions and regression coverage)

## Phase A baseline freeze (2026-04-22)

- **Priority stack:** `sessions.ts` -> `exec-approval.ts` / `exec-approvals.ts` -> structured transcript -> detached `/btw` -> observability (`debug.ts` / `logs.ts` / `health.ts`) -> `devices.ts` -> `skills.ts` depth.
- **Automation lane for chat semantics:** **option 3 (both)** -> WebView JSON-fixture `runRegressionChecks` + C++ gateway replay tests.
- **Destination confirmation for B-F:** no changes to matrix destinations; `sessions.ts` remains WebView-first, `exec-approval.ts` / `exec-approvals.ts` remain native MFC/C++ first, and gateway parity documentation remains split by embedded vs external shim mode.

## Re-check notes (2026-04-22)

- Verified in current `blazeclaw/BlazeClawMfc/web/chat` code: implemented surfaces include agents/tools/files/skills/channels/cron/dreaming/nodes/instances/usage plus assistant identity and control-ui-bootstrap adapter semantics.
- Remaining high-value gaps after re-check: sessions controller parity (`sessions.list/subscribe/compaction` workflows), debug/devices/logs/exec-approvals UI, richer structured transcript parity, and full OpenClaw Lit UX depth.

## Next handoff

Use this file as the authoritative decision matrix for implementation planning and keep it synchronized with:
- `docs/compare/ui.controllers.md`
- `blazeclaw/docs/OPERATOR_WEBVIEW_GATEWAY_TRANSPORT.md` (WebView ↔ gateway transport + canonical RPC naming)
- `blazeclaw/docs/GATEWAY_SERVER_METHODS_MECHANICAL_AUDIT.md`
- `docs/compare/agents.ts/AGENTS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (agents controller deep-dive and execution plan)
- `docs/compare/channels.ts/CHANNELS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (channels controller deep-dive and execution plan)
- `docs/compare/channels.types.ts/CHANNELS_TYPES_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (channels state-contract deep-dive and parity plan)
- `docs/compare/config/form-coerce.ts/FORM_COERCE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (config form coercion deep-dive and parity plan)
- `docs/compare/config/form-utils.ts/FORM_UTILS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (config form utility deep-dive and parity plan)
- `docs/compare/control-ui-bootstrap.ts/CONTROL_UI_BOOTSTRAP_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (control UI bootstrap deep-dive and parity plan)
- `docs/compare/cron.ts/CRON_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (cron controller deep-dive and parity plan)
- `docs/compare/dreaming.ts/DREAMING_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (dreaming controller deep-dive and parity plan)
- `docs/compare/nodes.ts/NODES_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (nodes controller deep-dive and parity plan)
- `docs/compare/presence.ts/PRESENCE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (presence controller deep-dive and parity plan)
- `docs/compare/usage.ts/USAGE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md` (usage controller deep-dive and parity plan)
